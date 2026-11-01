//! Session de dépannage, côté système (root, lancé par pkexec ou systemd) :
//! démarrage de Tailscale avec Tailscale SSH, arrêt programmé, prolongation,
//! nettoyage au démarrage de l'ordinateur. Et l'état de la session, lisible
//! par tous (fenêtre).
//!
//! Rien ne tourne en dehors d'une session : tailscaled n'est jamais activé au
//! démarrage, seulement démarré pendant la session puis arrêté, après
//! `tailscale logout` (la machine quitte le réseau Tailscale).

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use std::process::{Command, Stdio};
use std::time::{SystemTime, UNIX_EPOCH};

/// État de la session en cours (gardé après un redémarrage pour le nettoyage)
const STATE: &str = "/var/lib/packarch-depannage/session.json";
/// Tag Tailscale des machines dépannées (règles d'accès du réseau Tailscale)
pub const TAG: &str = "tag:depannage";
/// Unités systemd transitoires de la session
const UP_UNIT: &str = "packarch-depannage-up";
const STOP_UNIT: &str = "packarch-depannage-stop";
const BIN: &str = "/usr/bin/packarch-depannage";

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Saved {
    /// Fin de la session (secondes depuis 1970)
    pub end: u64,
    /// Nom de la machine dans le réseau Tailscale
    pub hostname: String,
}

/// État affiché par la fenêtre (packarch-depannage status)
#[derive(Serialize, Deserialize, Clone, Debug, Default, PartialEq)]
pub struct Status {
    pub active: bool,
    pub end: Option<u64>,
    pub hostname: String,
    /// État de Tailscale : NeedsLogin (en attente du lien), Running (prêt),
    /// Starting, Stopped… (vide si tailscaled ne tourne pas)
    pub backend: String,
    /// Lien de connexion à transmettre
    pub auth_url: Option<String>,
    /// Nom complet de la machine dans le réseau (une fois connectée)
    pub dns_name: Option<String>,
    /// Connexions SSH en cours (dépannage en cours)
    pub ssh_sessions: usize,
}

pub fn now() -> u64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0)
}

/// Nom de la machine dans le réseau : packarch-<nom d'hôte>
pub fn hostname() -> String {
    machine_name(&fs::read_to_string("/etc/hostname").unwrap_or_default())
}

/// packarch-<RAW> en minuscules, lettres, chiffres et tirets
fn machine_name(raw: &str) -> String {
    let name: String = raw
        .trim()
        .to_lowercase()
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '-' })
        .collect();
    let name = name.trim_matches('-');
    match name {
        "" => "packarch-ordinateur".into(),
        n if n.starts_with("packarch") => n.into(),
        n => format!("packarch-{n}"),
    }
}

fn load() -> Option<Saved> {
    serde_json::from_str(&fs::read_to_string(STATE).ok()?).ok()
}

fn save(s: &Saved) -> Result<(), String> {
    let p = Path::new(STATE);
    if let Some(d) = p.parent() {
        fs::create_dir_all(d).map_err(|e| format!("{} : {e}", d.display()))?;
    }
    fs::write(p, serde_json::to_string(s).unwrap_or_default()).map_err(|e| format!("{STATE} : {e}"))
}

fn run(cmd: &str, args: &[&str]) -> bool {
    Command::new(cmd)
        .args(args)
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .is_ok_and(|s| s.success())
}

fn tailscaled_active() -> bool {
    run("systemctl", &["is-active", "--quiet", "tailscaled"])
}

/// Sortie JSON de « tailscale status » (None si tailscaled ne répond pas)
fn tailscale_status() -> Option<serde_json::Value> {
    let out = Command::new("tailscale")
        .args(["status", "--json"])
        .stderr(Stdio::null())
        .output()
        .ok()?;
    serde_json::from_slice(&out.stdout).ok()
}

fn require_root() -> Result<(), String> {
    // SAFETY: getuid n'a pas d'effet de bord
    if unsafe { getuid() } != 0 {
        return Err("à lancer en root (pkexec packarch-depannage …)".into());
    }
    Ok(())
}

unsafe extern "C" {
    fn getuid() -> u32;
}

/// Programme l'arrêt de la session dans SECS secondes (remplace l'ancien)
fn schedule_stop(secs: u64) -> Result<(), String> {
    run("systemctl", &["stop", &format!("{STOP_UNIT}.timer")]);
    let delay = format!("--on-active={}s", secs.max(1));
    if !run("systemd-run", &[&format!("--unit={STOP_UNIT}"), "--collect", &delay, BIN, "stop"]) {
        return Err("impossible de programmer l'arrêt automatique (systemd-run)".into());
    }
    Ok(())
}

/// Démarre une session de MINUTES minutes
pub fn start(minutes: u64) -> Result<(), String> {
    require_root()?;
    if let Some(s) = load() {
        if s.end > now() {
            println!("Session déjà en cours ({}).", s.hostname);
            return Ok(());
        }
    } else if tailscaled_active() {
        // Tailscale utilisé pour autre chose sur cet ordinateur : on n'y touche pas
        return Err("Tailscale est déjà utilisé sur cet ordinateur, en dehors du dépannage".into());
    }
    let hostname = hostname();
    let end = now() + minutes * 60;
    save(&Saved { end, hostname: hostname.clone() })?;
    if !run("systemctl", &["start", "tailscaled"]) {
        let _ = fs::remove_file(STATE);
        return Err("impossible de démarrer tailscaled".into());
    }
    // tailscaled prêt à répondre
    for _ in 0..50 {
        if tailscale_status().is_some() {
            break;
        }
        std::thread::sleep(std::time::Duration::from_millis(200));
    }
    schedule_stop(minutes * 60)?;
    // « tailscale up » attend que le lien de connexion soit validé : il tourne
    // dans sa propre unité, la fenêtre lit le lien dans « tailscale status »
    run("systemctl", &["stop", &format!("{UP_UNIT}.service")]);
    let host = format!("--hostname={hostname}");
    let tags = format!("--advertise-tags={TAG}");
    if !run(
        "systemd-run",
        &[&format!("--unit={UP_UNIT}"), "--collect", "tailscale", "up", "--ssh", "--reset", &host, &tags, "--timeout=0"],
    ) {
        stop()?;
        return Err("impossible de lancer « tailscale up »".into());
    }
    println!("Session démarrée : {hostname}, fin dans {minutes} min.");
    Ok(())
}

/// Prolonge la session de MINUTES minutes
pub fn extend(minutes: u64) -> Result<(), String> {
    require_root()?;
    let mut s = load().ok_or("aucune session en cours")?;
    s.end = s.end.max(now()) + minutes * 60;
    save(&s)?;
    schedule_stop(s.end - now())?;
    println!("Session prolongée de {minutes} min.");
    Ok(())
}

/// Arrête la session : la machine quitte le réseau Tailscale, tailscaled est
/// arrêté. IF_ACTIVE : ne fait rien sans session (nettoyage au démarrage).
pub fn stop_with(if_active: bool) -> Result<(), String> {
    require_root()?;
    if if_active && load().is_none() {
        return Ok(());
    }
    run("systemctl", &["stop", &format!("{STOP_UNIT}.timer")]);
    run("systemctl", &["stop", &format!("{UP_UNIT}.service")]);
    if !tailscaled_active() {
        run("systemctl", &["start", "tailscaled"]);
    }
    // Retire la machine du réseau (sinon elle y reste, hors ligne)
    run("tailscale", &["logout"]);
    run("systemctl", &["stop", "tailscaled"]);
    let _ = fs::remove_file(STATE);
    println!("Session de dépannage terminée.");
    Ok(())
}

pub fn stop() -> Result<(), String> {
    stop_with(false)
}

/// Processus des connexions Tailscale SSH (« tailscaled be-child ssh »)
fn ssh_sessions() -> usize {
    fs::read_dir("/proc")
        .into_iter()
        .flatten()
        .flatten()
        .filter(|e| e.file_name().to_string_lossy().chars().all(|c| c.is_ascii_digit()))
        .filter_map(|e| fs::read(e.path().join("cmdline")).ok())
        .filter(|c| {
            let args: Vec<&[u8]> = c.split(|b| *b == 0).collect();
            args.windows(2).any(|w| w[0] == b"be-child" && w[1] == b"ssh")
        })
        .count()
}

/// État de la session (lisible sans être root)
pub fn status() -> Status {
    let Some(saved) = load() else {
        return Status { hostname: hostname(), ..Default::default() };
    };
    let mut st = Status {
        active: true,
        end: Some(saved.end),
        hostname: saved.hostname,
        ..Default::default()
    };
    if let Some(j) = tailscaled_active().then(tailscale_status).flatten() {
        st.backend = j["BackendState"].as_str().unwrap_or("").to_string();
        st.auth_url = j["AuthURL"].as_str().filter(|s| !s.is_empty()).map(str::to_string);
        st.dns_name = j["Self"]["DNSName"]
            .as_str()
            .map(|s| s.trim_end_matches('.').to_string())
            .filter(|s| !s.is_empty() && st.backend == "Running");
    }
    st.ssh_sessions = ssh_sessions();
    st
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn status_json_round_trip() {
        let s = Status { active: true, end: Some(10), hostname: "packarch-pc".into(), ..Default::default() };
        let back: Status = serde_json::from_str(&serde_json::to_string(&s).unwrap()).unwrap();
        assert_eq!(back, s);
    }

    #[test]
    fn machine_names() {
        assert_eq!(machine_name("PC-Maman\n"), "packarch-pc-maman");
        assert_eq!(machine_name("Salon de Mamie"), "packarch-salon-de-mamie");
        assert_eq!(machine_name("packarch"), "packarch");
        assert_eq!(machine_name("  \n"), "packarch-ordinateur");
        assert_eq!(machine_name("éé"), "packarch-ordinateur");
    }
}
