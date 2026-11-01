//! Session graphique de l'utilisateur, pour la personne qui dépanne (root,
//! par Tailscale SSH) : `session-run` lance une commande en tant que
//! l'utilisateur connecté, avec l'environnement de son bureau (écran, bus
//! D-Bus, XDG…) ; `info` décrit l'ordinateur.

use std::collections::HashMap;
use std::fs;
use std::os::unix::fs::MetadataExt;
use std::process::Command;

/// Variables reprises de l'environnement du bureau
const KEEP: &[&str] = &[
    "DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR", "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_DESKTOP", "XDG_SESSION_TYPE", "XDG_CONFIG_HOME", "XDG_DATA_DIRS", "DESKTOP_SESSION",
    "HOME", "USER", "LOGNAME", "SHELL", "LANG", "LC_ALL", "PATH",
];

/// Processus « racines » d'un bureau, préférés pour lire l'environnement
const SESSION_PROCS: &[&str] = &["xfce4-session", "openbox", "xfwm4", "polybar", "xfce4-panel"];

pub struct Desktop {
    pub user: String,
    pub uid: u32,
    pub env: Vec<(String, String)>,
}

impl Desktop {
    pub fn get(&self, key: &str) -> &str {
        self.env.iter().find(|(k, _)| k == key).map_or("", |(_, v)| v.as_str())
    }
}

fn user_name(uid: u32) -> Option<String> {
    fs::read_to_string("/etc/passwd").ok()?.lines().find_map(|l| {
        let f: Vec<&str> = l.split(':').collect();
        (f.len() > 2 && f[2].parse::<u32>().ok() == Some(uid)).then(|| f[0].to_string())
    })
}

/// Bureau de l'utilisateur connecté : l'environnement d'un de ses processus
/// graphiques (le processus de session de préférence)
pub fn find() -> Option<Desktop> {
    let mut best: Option<(bool, u32, HashMap<String, String>)> = None;
    for e in fs::read_dir("/proc").ok()?.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        if !name.chars().all(|c| c.is_ascii_digit()) {
            continue;
        }
        let Ok(meta) = e.metadata() else { continue };
        let uid = meta.uid();
        if uid < 1000 || uid == 65534 {
            continue;
        }
        let Ok(raw) = fs::read(e.path().join("environ")) else { continue };
        let env: HashMap<String, String> = raw
            .split(|b| *b == 0)
            .filter_map(|kv| {
                let s = String::from_utf8_lossy(kv);
                let (k, v) = s.split_once('=')?;
                Some((k.to_string(), v.to_string()))
            })
            .collect();
        if !env.contains_key("DISPLAY") {
            continue;
        }
        let comm = fs::read_to_string(e.path().join("comm")).unwrap_or_default();
        let preferred = SESSION_PROCS.contains(&comm.trim());
        if best.as_ref().is_none_or(|(p, _, _)| preferred && !p) {
            best = Some((preferred, uid, env));
            if preferred {
                break;
            }
        }
    }
    let (_, uid, env) = best?;
    let user = env.get("USER").cloned().or_else(|| user_name(uid))?;
    let mut kept: Vec<(String, String)> =
        KEEP.iter().filter_map(|k| env.get(*k).map(|v| (k.to_string(), v.clone()))).collect();
    if !kept.iter().any(|(k, _)| k == "PATH") {
        kept.push(("PATH".into(), "/usr/local/bin:/usr/bin".into()));
    }
    Some(Desktop { user, uid, env: kept })
}

/// packarch-depannage session-run COMMANDE… : la commande (passée à sh -c)
/// en tant que l'utilisateur, dans son bureau. Rend son code de sortie.
pub fn session_run(cmd: &[String]) -> Result<i32, String> {
    if cmd.is_empty() {
        return Err("usage : packarch-depannage session-run COMMANDE…".into());
    }
    let d = find().ok_or("aucune session graphique trouvée (personne n'est connecté ?)")?;
    let mut args: Vec<String> = vec!["-u".into(), d.user.clone(), "--".into(), "env".into(), "-i".into()];
    args.extend(d.env.iter().map(|(k, v)| format!("{k}={v}")));
    args.extend(["sh".into(), "-c".into(), cmd.join(" ")]);
    let status = Command::new("runuser").args(&args).status().map_err(|e| format!("runuser : {e}"))?;
    Ok(status.code().unwrap_or(1))
}

fn os_name() -> String {
    fs::read_to_string("/etc/os-release")
        .ok()
        .and_then(|t| {
            t.lines()
                .find_map(|l| l.strip_prefix("PRETTY_NAME=").map(|v| v.trim_matches('"').to_string()))
        })
        .unwrap_or_else(|| "?".into())
}

fn output(cmd: &str, args: &[&str]) -> String {
    Command::new(cmd)
        .args(args)
        .output()
        .map(|o| String::from_utf8_lossy(&o.stdout).trim().to_string())
        .unwrap_or_default()
}

/// packarch-depannage info : description de l'ordinateur pour la personne
/// qui dépanne
pub fn info() {
    let st = crate::session::status();
    println!("Machine        : {}", st.dns_name.as_deref().unwrap_or(&st.hostname));
    println!("Système        : {}", os_name());
    println!("Noyau          : {}", output("uname", &["-r"]));
    match find() {
        Some(d) => {
            println!("Utilisateur    : {} (uid {}, {})", d.user, d.uid, d.get("HOME"));
            println!("Bureau         : {}", d.get("XDG_CURRENT_DESKTOP"));
            println!("Écran          : {}", d.get("DISPLAY"));
            let theme = output(
                "runuser",
                &["-u", &d.user, "--", "env", &format!("HOME={}", d.get("HOME")), &format!("XDG_CURRENT_DESKTOP={}", d.get("XDG_CURRENT_DESKTOP")), "packarch-skin", "current"],
            );
            if !theme.is_empty() {
                println!("Thème          : {theme} (packarch-skin)");
            }
        }
        None => println!("Utilisateur    : aucune session graphique"),
    }
    if let Some(end) = st.end {
        let left = end.saturating_sub(crate::session::now()) / 60;
        println!("Fin de session : dans {left} min");
    }
    println!();
    println!("Commandes dans le bureau de l'utilisateur :");
    println!("  packarch-depannage session-run 'COMMANDE'");
}
