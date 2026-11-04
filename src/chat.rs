//! Tchat entre la personne dépannée et la personne qui dépanne : un fichier
//! dans le dossier de la session (en mémoire, effacé à l'arrêt de la session,
//! rien sur le disque), un message JSON par ligne.
//!
//! L'ordinateur dépanné ne peut joindre personne (règles du réseau Tailscale) :
//! c'est la personne qui dépanne qui vient lire et écrire ce fichier par
//! Tailscale SSH (`chat-follow`, `chat-say`), depuis sa fenêtre. Le fichier
//! appartient à l'utilisateur, pour que sa fenêtre y écrive sans mot de passe ;
//! le dossier appartient à root, il ne peut pas le remplacer par un lien.

use crate::{desktop, session};
use serde::{Deserialize, Serialize};
use std::fs::{self, File, OpenOptions};
use std::io::{Read, Seek, SeekFrom, Write};
use std::os::unix::fs::{MetadataExt, OpenOptionsExt, fchown};
use std::path::Path;
use std::time::Duration;

pub const CHAT: &str = "/run/packarch-depannage/chat.jsonl";
/// Présent quand la session s'arrête : chat-follow se termine normalement, avant
/// que la connexion ne soit coupée (effacé avec le dossier de la session)
const STOPPING: &str = "/run/packarch-depannage/stopping";
/// Attente de la fin de chat-follow à l'arrêt de la session : relectures, délai
/// laissé à son code de sortie pour parvenir à la fenêtre qui dépanne
const STOP_WAIT: Duration = Duration::from_millis(100);
const STOP_WAIT_MAX: u32 = 20;
const STOP_GRACE: Duration = Duration::from_millis(400);
/// Longueur maximale d'un message (caractères)
const MAX_CHARS: usize = 2000;
/// Délai entre deux relectures, et entre deux lignes vides de chat-follow : une
/// écriture qui échoue révèle une connexion perdue
const POLL: Duration = Duration::from_secs(1);
const HEARTBEAT_POLLS: u32 = 15;
/// Nom du processus de la fenêtre (/proc/PID/comm, tronqué à 15 caractères)
const COMM: &str = "packarch-depann";

#[derive(Serialize, Deserialize, Clone, Copy, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum From {
    User,
    Helper,
}

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Message {
    pub from: From,
    pub time: u64,
    pub text: String,
}

/// Texte d'un message : sans caractères de contrôle, sans blancs autour, borné
pub fn clean(text: &str) -> String {
    let text: String = text
        .chars()
        .map(|c| if c == '\t' { ' ' } else { c })
        .filter(|c| !c.is_control() || *c == '\n')
        .take(MAX_CHARS)
        .collect();
    text.trim().to_string()
}

/// Message d'une ligne du fichier (ou de chat-follow) ; None si elle n'en
/// contient pas
pub fn parse_line(line: &str) -> Option<Message> {
    let mut m: Message = serde_json::from_str(line).ok()?;
    m.text = clean(&m.text);
    (!m.text.is_empty()).then_some(m)
}

/// Lignes complètes écrites depuis la position POS, qui avance
pub fn read_new(path: &str, pos: &mut u64) -> Vec<Message> {
    let Ok(mut f) = File::open(path) else { return Vec::new() };
    if f.metadata().map(|m| m.len()).unwrap_or(0) < *pos {
        *pos = 0;
    }
    let mut buf = Vec::new();
    if f.seek(SeekFrom::Start(*pos)).is_err() || f.read_to_end(&mut buf).is_err() {
        return Vec::new();
    }
    let Some(end) = buf.iter().rposition(|b| *b == b'\n') else { return Vec::new() };
    *pos += end as u64 + 1;
    String::from_utf8_lossy(&buf[..end]).lines().filter_map(parse_line).collect()
}

/// Ajoute un message (une seule écriture, en fin de fichier)
pub fn append_to(path: &str, from: From, text: &str) -> Result<(), String> {
    let text = clean(text);
    if text.is_empty() {
        return Ok(());
    }
    let msg = Message { from, time: session::now(), text };
    let line = serde_json::to_string(&msg).map_err(|e| e.to_string())? + "\n";
    let mut f = OpenOptions::new().append(true).open(path).map_err(|e| e.to_string())?;
    f.write_all(line.as_bytes()).map_err(|e| e.to_string())
}

/// Crée le fichier du tchat et le donne à l'utilisateur (root) : celui qui a
/// autorisé le dépannage (pkexec), sinon celui du bureau
pub fn ensure() -> Result<(), String> {
    let f = OpenOptions::new()
        .append(true)
        .create(true)
        .mode(0o600)
        .open(Path::new(CHAT))
        .map_err(|e| format!("{CHAT} : {e}"))?;
    if f.metadata().map_err(|e| e.to_string())?.uid() != 0 {
        return Ok(());
    }
    let pkexec = std::env::var("PKEXEC_UID").ok().and_then(|u| u.parse::<u32>().ok()).filter(|u| *u != 0);
    if let Some(uid) = pkexec.or_else(|| desktop::find().map(|d| d.uid)) {
        fchown(&f, Some(uid), None).map_err(|e| format!("{CHAT} : {e}"))?;
    }
    Ok(())
}

/// Processus chat-follow en cours
fn followers() -> usize {
    fs::read_dir("/proc")
        .into_iter()
        .flatten()
        .flatten()
        .filter_map(|e| fs::read(e.path().join("cmdline")).ok())
        .filter(|c| c.split(|b| *b == 0).any(|a| a == b"chat-follow"))
        .count()
}

/// Annonce la fin de la session à la fenêtre de la personne qui dépanne, tant que
/// la connexion existe : sans cela, elle ne verrait qu'une connexion perdue
pub fn announce_stop() {
    if followers() == 0 || fs::write(STOPPING, "").is_err() {
        return;
    }
    for _ in 0..STOP_WAIT_MAX {
        if followers() == 0 {
            std::thread::sleep(STOP_GRACE);
            return;
        }
        std::thread::sleep(STOP_WAIT);
    }
}

/// La fenêtre de l'utilisateur est-elle ouverte ?
fn window_open() -> bool {
    fs::read_dir("/proc").into_iter().flatten().flatten().any(|e| {
        e.metadata().is_ok_and(|m| m.uid() >= 1000)
            && fs::read_to_string(e.path().join("comm")).is_ok_and(|c| c.trim() == COMM)
    })
}

/// packarch-depannage chat-say (root) : envoie le texte lu sur l'entrée
/// standard (pas d'argument : le texte ne passe pas par le shell distant) et
/// rouvre la fenêtre de l'utilisateur si elle est fermée
pub fn say() -> Result<(), String> {
    session::require_root()?;
    if !session::active() {
        return Err("aucune session en cours".into());
    }
    let mut text = String::new();
    std::io::stdin().read_to_string(&mut text).map_err(|e| e.to_string())?;
    ensure()?;
    append_to(CHAT, From::Helper, &text)?;
    if !window_open() {
        let _ = desktop::session_run(&["cd ~ && setsid -f packarch-depannage >/dev/null 2>&1".to_string()]);
    }
    Ok(())
}

/// packarch-depannage chat-follow (root) : écrit les messages, depuis le début,
/// au fil de l'eau, jusqu'à la fin de la session ou de la connexion. Une ligne
/// vide de temps en temps (la première : connexion établie).
pub fn follow() -> Result<(), String> {
    session::require_root()?;
    ensure()?;
    let mut out = std::io::stdout().lock();
    let mut pos = 0;
    let mut polls = 0;
    while session::active() && !Path::new(STOPPING).exists() {
        let mut text = String::new();
        for m in read_new(CHAT, &mut pos) {
            text += &serde_json::to_string(&m).map_err(|e| e.to_string())?;
            text.push('\n');
        }
        if polls % HEARTBEAT_POLLS == 0 {
            text.push('\n');
        }
        if !text.is_empty() && (out.write_all(text.as_bytes()).is_err() || out.flush().is_err()) {
            return Ok(());
        }
        polls = polls.wrapping_add(1);
        std::thread::sleep(POLL);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cleaned_text() {
        assert_eq!(clean("  bonjour\x1b[31m\ttoi\u{9b}\n "), "bonjour[31m toi");
        assert_eq!(clean("a\nb"), "a\nb");
        assert_eq!(clean(&"é".repeat(MAX_CHARS + 5)).chars().count(), MAX_CHARS);
    }

    #[test]
    fn invalid_lines_ignored() {
        assert_eq!(parse_line(""), None);
        assert_eq!(parse_line("{pas du json"), None);
        assert_eq!(parse_line(r#"{"from":"root","time":1,"text":"x"}"#), None);
        assert_eq!(parse_line(r#"{"from":"user","time":1,"text":" \u001b "}"#), None);
        assert_eq!(
            parse_line(r#"{"from":"helper","time":1,"text":"ok"}"#),
            Some(Message { from: From::Helper, time: 1, text: "ok".into() })
        );
    }

    #[test]
    fn appended_then_read_once() {
        let dir = std::env::temp_dir().join(format!("depannage-chat-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let p = dir.join("chat.jsonl");
        fs::write(&p, "").unwrap();
        let p = p.to_str().unwrap();
        let mut pos = 0;
        append_to(p, From::User, "bonjour\nça va ?").unwrap();
        append_to(p, From::User, "   ").unwrap();
        let got = read_new(p, &mut pos);
        assert_eq!(got.len(), 1);
        assert_eq!(got[0].text, "bonjour\nça va ?");
        assert!(read_new(p, &mut pos).is_empty());
        // Ligne en cours d'écriture : attendue jusqu'à sa fin
        fs::OpenOptions::new().append(true).open(p).unwrap().write_all(br#"{"from":"helper","#).unwrap();
        assert!(read_new(p, &mut pos).is_empty());
        fs::OpenOptions::new().append(true).open(p).unwrap().write_all(b"\"time\":1,\"text\":\"oui\"}\n").unwrap();
        assert_eq!(read_new(p, &mut pos)[0].text, "oui");
        // Fichier recréé (nouvelle session) : relu depuis le début
        fs::write(p, "").unwrap();
        append_to(p, From::User, "x").unwrap();
        assert_eq!(read_new(p, &mut pos)[0].text, "x");
        fs::remove_dir_all(&dir).unwrap();
    }
}
