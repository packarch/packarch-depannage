//! Tout ce qui attend (commandes, ssh, fichier du tchat) tourne dans des fils
//! séparés, qui écrivent à la fenêtre par un canal : egui redessine la fenêtre à
//! chaque image, rien ne doit y bloquer. Ces fils continuent quand la fenêtre est
//! réduite (egui ne redessine plus) : les notifications partent d'ici.

use crate::chat::{self, From, Message};
use crate::session::{self, Machine, Status};
use eframe::egui;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::Path;
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::Sender;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

/// Délai avant de retenter une connexion perdue
const RETRY: Duration = Duration::from_secs(5);
/// Code de sortie d'une version sans tchat (commande inconnue)
const EXIT_USAGE: i32 = 2;
const BIN: &str = "/usr/bin/packarch-depannage";
/// Titre et texte d'une notification : ceux de Packarch (dunst, thèmes Openbox)
/// ont une hauteur fixe de deux lignes d'environ 38 caractères, le reste est coupé
const NOTIFICATION_TITLE: &str = "Dépannage : message reçu";
const NOTIFICATION_CHARS: usize = 36;

pub enum Event {
    Status(Status),
    Machines(Result<Vec<Machine>, String>),
    UserMessage(Message),
    UserChatReset,
    Conversation(u64, ConversationEvent),
    Privileged(Result<(), String>, &'static str),
}

pub enum ConversationEvent {
    Connected,
    Message(Message),
    Lost(String),
    Finished(String),
    NotSent(String, String),
}

/// Canal vers la fenêtre, qui est réveillée à chaque événement
#[derive(Clone)]
pub struct Bus {
    pub tx: Sender<Event>,
    pub ctx: egui::Context,
    /// La fenêtre est au premier plan
    pub focused: Arc<AtomicBool>,
    /// La liste des ordinateurs à dépanner est à l'écran
    pub want_machines: Arc<AtomicBool>,
}

impl Bus {
    fn send(&self, event: Event) {
        let _ = self.tx.send(event);
        self.ctx.request_repaint();
    }

    fn notify_if_hidden(&self, text: &str) {
        if self.focused.load(Ordering::Relaxed) {
            return;
        }
        let _ = Command::new("notify-send")
            .args(["--app-name=packarch-depannage", "--", NOTIFICATION_TITLE, &notification_body(text)])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
    }
}

/// Début du message, sur une ligne, en texte échappé (le texte des notifications
/// est interprété comme du balisage)
fn notification_body(text: &str) -> String {
    let line = text.split_whitespace().collect::<Vec<_>>().join(" ");
    let mut short: String = line.chars().take(NOTIFICATION_CHARS).collect();
    if line.chars().count() > NOTIFICATION_CHARS {
        short.push('…');
    }
    short.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;")
}

/// État de la session (deux secondes), liste des ordinateurs si elle est à l'écran
pub fn watch_status(bus: Bus) {
    thread::spawn(move || {
        loop {
            bus.send(Event::Status(session::status()));
            if bus.want_machines.load(Ordering::Relaxed) {
                bus.send(Event::Machines(session::machines_to_help()));
            }
            thread::sleep(Duration::from_secs(2));
        }
    });
}

pub fn refresh_machines(bus: Bus) {
    thread::spawn(move || bus.send(Event::Machines(session::machines_to_help())));
}

/// Tchat de l'ordinateur dépanné : le fichier de la session
pub fn watch_user_chat(bus: Bus) {
    thread::spawn(move || {
        let mut pos = 0;
        let mut live = false;
        loop {
            if !Path::new(chat::CHAT).exists() {
                if pos > 0 {
                    bus.send(Event::UserChatReset);
                }
                pos = 0;
                // Fichier neuf : ses messages arriveront en direct
                live = true;
            }
            for m in chat::read_new(chat::CHAT, &mut pos) {
                if live && m.from == From::Helper {
                    bus.notify_if_hidden(&m.text);
                }
                bus.send(Event::UserMessage(m));
            }
            live = true;
            thread::sleep(Duration::from_millis(500));
        }
    });
}

/// pkexec packarch-depannage ARGS…
pub fn privileged(bus: Bus, args: Vec<&'static str>, failure: &'static str) {
    thread::spawn(move || {
        let out = Command::new("pkexec").arg(BIN).args(&args).stdin(Stdio::null()).stdout(Stdio::null()).output();
        let result = match out {
            Err(e) => Err(e.to_string()),
            Ok(o) if o.status.success() => Ok(()),
            Ok(o) => Err(failure_reason(o.status.code(), &String::from_utf8_lossy(&o.stderr))),
        };
        bus.send(Event::Privileged(result, failure));
    });
}

/// Raison de l'échec : la dernière ligne d'erreur du programme, ou le code de
/// sortie de pkexec
fn failure_reason(code: Option<i32>, stderr: &str) -> String {
    if let Some(line) = stderr.lines().map(str::trim).filter(|l| !l.is_empty()).last() {
        return line.strip_prefix("packarch-depannage : ").unwrap_or(line).to_string();
    }
    match code {
        Some(126) => "autorisation refusée ou annulée".into(),
        Some(127) => "authentification impossible (agent polkit absent ?)".into(),
        Some(c) => format!("code de sortie {c}"),
        None => "arrêté par un signal".into(),
    }
}

/// Commande ssh vers MACHINE. Pas de vérification de la clé d'hôte : elle change
/// à chaque session (état en mémoire), et le réseau Tailscale authentifie déjà
/// la machine jointe par son adresse.
fn ssh(machine: &Machine, opts: &[&str], command: &str) -> Command {
    let mut cmd = Command::new("ssh");
    for o in ["BatchMode=yes", "ConnectTimeout=15", "StrictHostKeyChecking=no", "UserKnownHostsFile=/dev/null", "LogLevel=ERROR", "ServerAliveInterval=15", "ServerAliveCountMax=3"] {
        cmd.args(["-o", o]);
    }
    cmd.args(opts).arg(format!("root@{}", machine.ip)).arg(format!("packarch-depannage {command}"));
    cmd
}

/// Conversation avec un ordinateur : reçoit tant qu'elle est ouverte, se
/// reconnecte si besoin
pub struct Conversation {
    pub id: u64,
    pub machine: Machine,
    open: Arc<AtomicBool>,
    follower: Arc<Mutex<Option<Child>>>,
    bus: Bus,
}

impl Conversation {
    pub fn start(id: u64, machine: Machine, bus: Bus) -> Self {
        let conv = Self {
            id,
            machine,
            open: Arc::new(AtomicBool::new(true)),
            follower: Arc::new(Mutex::new(None)),
            bus,
        };
        let (machine, open, follower, bus) = (conv.machine.clone(), conv.open.clone(), conv.follower.clone(), conv.bus.clone());
        thread::spawn(move || receive(id, machine, open, follower, bus));
        conv
    }

    pub fn send(&self, text: String) {
        let (id, machine, bus) = (self.id, self.machine.clone(), self.bus.clone());
        thread::spawn(move || {
            if let Err(reason) = send(&machine, &text) {
                bus.send(Event::Conversation(id, ConversationEvent::NotSent(text, reason)));
            }
        });
    }

    pub fn close(&self) {
        self.open.store(false, Ordering::Relaxed);
        if let Some(mut child) = self.follower.lock().unwrap().take() {
            let _ = child.kill();
            let _ = child.wait();
        }
    }
}

fn last_line(text: &str) -> String {
    text.lines().map(str::trim).filter(|l| !l.is_empty()).last().unwrap_or("").to_string()
}

fn send(machine: &Machine, text: &str) -> Result<(), String> {
    let mut child = ssh(machine, &[], "chat-say")
        .stdin(Stdio::piped())
        .stdout(Stdio::null())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|e| e.to_string())?;
    if let Some(mut stdin) = child.stdin.take() {
        // En cas d'échec, l'erreur de ssh dit mieux pourquoi
        let _ = stdin.write_all(text.as_bytes());
    }
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    if out.status.success() {
        return Ok(());
    }
    let reason = last_line(&String::from_utf8_lossy(&out.stderr));
    Err(if reason.is_empty() { "échec".into() } else { reason })
}

fn receive(id: u64, machine: Machine, open: Arc<AtomicBool>, follower: Arc<Mutex<Option<Child>>>, bus: Bus) {
    let say = |e| bus.send(Event::Conversation(id, e));
    // Chaque connexion redonne tout l'historique : déjà affiché, il est sauté
    let mut shown = 0;
    while open.load(Ordering::Relaxed) {
        let spawned = ssh(&machine, &["-n"], "chat-follow")
            .stdin(Stdio::null())
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn();
        let mut child = match spawned {
            Ok(c) => c,
            Err(e) => return say(ConversationEvent::Finished(format!("Connexion impossible : {e}"))),
        };
        let stdout = child.stdout.take();
        let stderr = child.stderr.take();
        *follower.lock().unwrap() = Some(child);
        let mut seen = 0;
        if let Some(out) = stdout {
            for line in BufReader::new(out).lines() {
                let Ok(line) = line else { break };
                // Une ligne, même vide : la connexion est établie
                say(ConversationEvent::Connected);
                if let Some(m) = chat::parse_line(&line) {
                    seen += 1;
                    if seen > shown {
                        shown = seen;
                        if m.from == From::User {
                            bus.notify_if_hidden(&m.text);
                        }
                        say(ConversationEvent::Message(m));
                    }
                }
            }
        }
        let mut error = String::new();
        if let Some(mut err) = stderr {
            let _ = err.read_to_string(&mut error);
        }
        // Absent : la conversation a été fermée entre-temps
        let Some(mut child) = follower.lock().unwrap().take() else { return };
        let status = child.wait();
        if !open.load(Ordering::Relaxed) {
            return;
        }
        let code = status.as_ref().ok().and_then(|s| s.code());
        let gone = session::machines_to_help().is_ok_and(|l| !l.contains(&machine));
        if gone || code == Some(0) {
            return say(ConversationEvent::Finished("La session de dépannage est terminée.".into()));
        }
        if code == Some(EXIT_USAGE) {
            return say(ConversationEvent::Finished("Cet ordinateur doit d'abord être mis à jour pour avoir le tchat.".into()));
        }
        say(ConversationEvent::Lost(last_line(&error)));
        thread::sleep(RETRY);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn notification_fits_two_lines() {
        assert!(NOTIFICATION_TITLE.chars().count() <= 38);
        assert_eq!(notification_body("  a\nb  c "), "a b c");
        assert_eq!(notification_body(&"é".repeat(36)), "é".repeat(36));
        assert_eq!(notification_body(&"é".repeat(37)), "é".repeat(36) + "…");
        assert_eq!(notification_body("a <b> & c"), "a &lt;b&gt; &amp; c");
    }

    #[test]
    fn failure_reasons() {
        assert_eq!(failure_reason(Some(1), "x\npackarch-depannage : refusé\n"), "refusé");
        assert_eq!(failure_reason(Some(126), ""), "autorisation refusée ou annulée");
    }
}
