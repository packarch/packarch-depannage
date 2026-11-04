//! packarch-depannage : dépannage à distance temporaire de Packarch.
//!
//! La fenêtre (sans argument) démarre, sur demande de l'utilisateur et avec
//! son mot de passe (pkexec), une session Tailscale avec Tailscale SSH : elle
//! affiche le lien de connexion à transmettre à la personne qui dépanne, qui
//! l'ouvre avec son compte Tailscale ; l'ordinateur rejoint alors son réseau
//! et elle peut s'y connecter en SSH (root). La session s'arrête toute seule
//! (minuterie systemd), ou avec le bouton « Arrêter maintenant ».

mod chat;
mod desktop;
mod session;
mod ui;
mod worker;

use std::process::ExitCode;

const USAGE: &str = "\
Usage : packarch-depannage [COMMANDE]

  (sans commande)         fenêtre du dépannage à distance
  status                  état de la session (JSON)
  info                    description de l'ordinateur (pour la personne qui dépanne)
  session-run COMMANDE…   lance COMMANDE dans le bureau de l'utilisateur connecté (root)

En root (pkexec, ou par la fenêtre) :
  start [MINUTES]         démarre une session (60 min par défaut)
  extend [MINUTES]        prolonge la session (60 min par défaut)
  stop                    arrête la session
  cleanup                 arrête une session restée ouverte (démarrage de l'ordinateur)
  chat-say                envoie à l'utilisateur le message lu sur l'entrée standard
  chat-follow             affiche les messages du tchat au fil de l'eau (JSON)";

fn minutes(arg: Option<&String>) -> Result<u64, String> {
    match arg {
        None => Ok(60),
        Some(a) => a.parse::<u64>().ok().filter(|m| (1..=24 * 60).contains(m)).ok_or(format!("durée invalide : {a}")),
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let result = match args.first().map(String::as_str) {
        None => {
            return ui::run();
        }
        Some("status") => {
            println!("{}", serde_json::to_string_pretty(&session::status()).unwrap_or_default());
            Ok(())
        }
        Some("info") => {
            desktop::info();
            Ok(())
        }
        Some("session-run") => match desktop::session_run(&args[1..]) {
            Ok(code) => return ExitCode::from(code.clamp(0, 255) as u8),
            Err(e) => Err(e),
        },
        Some("start") => minutes(args.get(1)).and_then(session::start),
        Some("extend") => minutes(args.get(1)).and_then(session::extend),
        Some("stop") => session::stop(),
        Some("cleanup") => session::stop_with(true),
        Some("chat-say") => chat::say(),
        Some("chat-follow") => chat::follow(),
        Some("-h" | "--help" | "help") => {
            println!("{USAGE}");
            Ok(())
        }
        Some(_) => {
            eprintln!("{USAGE}");
            return ExitCode::from(2);
        }
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("packarch-depannage : {e}");
            ExitCode::FAILURE
        }
    }
}
