//! Côté personne qui dépanne : la liste des ordinateurs à dépanner de son réseau
//! Tailscale, et le tchat avec l'un d'eux. Tout passe par Tailscale SSH (commande
//! `ssh`), sans rien à taper : chat-follow pour recevoir, chat-say pour envoyer.

use crate::chat::{self, From};
use crate::chat_view::ChatView;
use crate::session::{self, Machine};
use adw::prelude::*;
use gtk::{gio, glib};
use std::cell::{Cell, RefCell};
use std::ffi::OsStr;
use std::rc::Rc;

/// Délai avant de retenter une connexion perdue (secondes)
const RETRY_SECS: u32 = 5;
/// Code de sortie d'une version sans tchat (commande inconnue)
const EXIT_USAGE: i32 = 2;

/// Commande ssh vers MACHINE. Pas de vérification de la clé d'hôte : elle change
/// à chaque session (état en mémoire), et le réseau Tailscale authentifie déjà
/// la machine jointe par son adresse.
fn ssh(machine: &Machine, opts: &[&str], command: &str) -> Vec<String> {
    let mut argv: Vec<String> = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=15", "-o", "StrictHostKeyChecking=no"]
        .into_iter()
        .chain(["-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR"])
        .chain(["-o", "ServerAliveInterval=15", "-o", "ServerAliveCountMax=3"])
        .chain(opts.iter().copied())
        .map(String::from)
        .collect();
    argv.push(format!("root@{}", machine.ip));
    argv.push(format!("packarch-depannage {command}"));
    argv
}

fn spawn(argv: &[String], flags: gio::SubprocessFlags) -> Result<gio::Subprocess, glib::Error> {
    let argv: Vec<&OsStr> = argv.iter().map(OsStr::new).collect();
    gio::Subprocess::newv(&argv, flags)
}

/// Une conversation : reçoit tant qu'elle est ouverte, se reconnecte si besoin
struct Conversation {
    machine: Machine,
    view: ChatView,
    toasts: adw::ToastOverlay,
    open: Cell<bool>,
    follower: RefCell<Option<gio::Subprocess>>,
}

impl Conversation {
    fn close(&self) {
        self.open.set(false);
        if let Some(p) = self.follower.borrow_mut().take() {
            p.force_exit();
        }
    }

    fn finished(&self, text: &str) {
        self.view.set_status(Some(text));
        self.view.set_sendable(false);
    }

    fn send(self: &Rc<Self>, text: String) {
        let flags = gio::SubprocessFlags::STDIN_PIPE | gio::SubprocessFlags::STDOUT_SILENCE | gio::SubprocessFlags::STDERR_PIPE;
        let proc = match spawn(&ssh(&self.machine, &[], "chat-say"), flags) {
            Ok(p) => p,
            Err(e) => return self.not_sent(&text, &e.to_string()),
        };
        let conv = self.clone();
        glib::spawn_future_local(async move {
            let result = proc.communicate_utf8_future(Some(text.clone())).await;
            if !proc.is_successful() {
                let reason = match result {
                    Ok((_, err)) => err.map(|s| last_line(&s)).unwrap_or_default(),
                    Err(e) => e.to_string(),
                };
                conv.not_sent(&text, &reason);
            }
        });
    }

    fn not_sent(&self, text: &str, reason: &str) {
        self.view.restore(text);
        self.toasts.add_toast(adw::Toast::builder().title(format!("Message non envoyé : {reason}")).timeout(10).build());
    }

    /// Reçoit les messages jusqu'à la fermeture de la conversation
    async fn receive(self: Rc<Self>) {
        // Chaque connexion redonne tout l'historique : déjà affiché, il est sauté
        let mut shown = 0;
        while self.open.get() {
            let flags = gio::SubprocessFlags::STDOUT_PIPE | gio::SubprocessFlags::STDERR_PIPE;
            let proc = match spawn(&ssh(&self.machine, &["-n"], "chat-follow"), flags) {
                Ok(p) => p,
                Err(e) => return self.finished(&format!("Connexion impossible : {e}")),
            };
            *self.follower.borrow_mut() = Some(proc.clone());
            let lines = proc.stdout_pipe().map(|s| gio::DataInputStream::new(&s));
            let mut seen = 0;
            while let Some(lines) = &lines {
                let Ok(Some(line)) = lines.read_line_utf8_future(glib::Priority::DEFAULT).await else {
                    break;
                };
                // Une ligne, même vide : la connexion est établie
                self.view.set_status(None);
                self.view.set_sendable(true);
                if let Some(m) = chat::parse_line(&line) {
                    seen += 1;
                    if seen > shown {
                        shown = seen;
                        self.view.add(&m, true);
                    }
                }
            }
            let mut error = String::new();
            if let Some(err) = proc.stderr_pipe().map(|s| gio::DataInputStream::new(&s)) {
                while let Ok(Some(line)) = err.read_line_utf8_future(glib::Priority::DEFAULT).await {
                    if !line.trim().is_empty() {
                        error = line.trim().to_string();
                    }
                }
            }
            let _ = proc.wait_future().await;
            if !self.open.get() {
                return;
            }
            let gone = session::machines_to_help().is_ok_and(|l| !l.contains(&self.machine));
            if gone || proc.is_successful() {
                return self.finished("La session de dépannage est terminée.");
            }
            if proc.has_exited() && proc.exit_status() == EXIT_USAGE {
                return self.finished("Cet ordinateur doit d'abord être mis à jour pour avoir le tchat.");
            }
            self.view.set_sendable(false);
            self.view.set_status(Some(&format!("Connexion perdue, nouvel essai… {error}")));
            glib::timeout_future_seconds(RETRY_SECS).await;
        }
    }
}

fn last_line(text: &str) -> String {
    text.lines().map(str::trim).filter(|l| !l.is_empty()).last().unwrap_or("échec").to_string()
}

fn with_header(title: &str, child: &impl IsA<gtk::Widget>) -> adw::NavigationPage {
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(child));
    adw::NavigationPage::new(&toolbar, title)
}

/// Page « Dépanner quelqu'un »
pub struct Helper {
    nav: adw::NavigationView,
    toasts: adw::ToastOverlay,
    page: adw::NavigationPage,
    status: adw::StatusPage,
    list: gtk::ListBox,
    shown: RefCell<Vec<Machine>>,
    conversation: RefCell<Option<Rc<Conversation>>>,
}

impl Helper {
    pub fn new(nav: &adw::NavigationView, toasts: &adw::ToastOverlay) -> Rc<Self> {
        let list = gtk::ListBox::new();
        list.add_css_class("boxed-list");
        list.set_selection_mode(gtk::SelectionMode::None);
        list.set_visible(false);
        let status = adw::StatusPage::builder().child(&list).build();
        let helper = Rc::new(Self {
            nav: nav.clone(),
            toasts: toasts.clone(),
            page: with_header("Dépanner quelqu'un", &status),
            status,
            list,
            shown: RefCell::new(Vec::new()),
            conversation: RefCell::new(None),
        });
        {
            let helper = helper.clone();
            nav.connect_popped(move |_, _| helper.close_conversation());
        }
        helper
    }

    pub fn open(self: &Rc<Self>) {
        self.nav.push(&self.page);
        self.refresh();
    }

    pub fn close_conversation(&self) {
        if let Some(c) = self.conversation.borrow_mut().take() {
            c.close();
        }
    }

    /// Relit la liste des ordinateurs si elle est à l'écran
    pub fn tick(self: &Rc<Self>) {
        if self.nav.visible_page().as_ref() == Some(&self.page) {
            self.refresh();
        }
    }

    fn refresh(self: &Rc<Self>) {
        let (icon, title, text, machines) = match session::machines_to_help() {
            Err(e) => ("network-offline-symbolic", "Tailscale n'est pas connecté", e, Vec::new()),
            Ok(m) if m.is_empty() => (
                "preferences-desktop-remote-desktop",
                "Aucun ordinateur à dépanner",
                "La personne doit ouvrir « Dépannage à distance », cliquer sur « Autoriser le dépannage » \
                 et vous envoyer le lien. Ouvrez ce lien avec votre compte Tailscale : son ordinateur \
                 apparaîtra ici."
                    .to_string(),
                m,
            ),
            Ok(m) => (
                "preferences-desktop-remote-desktop",
                "Ordinateurs à dépanner",
                "Choisissez un ordinateur pour discuter avec la personne.".to_string(),
                m,
            ),
        };
        self.status.set_icon_name(Some(icon));
        self.status.set_title(title);
        self.status.set_description(Some(&text));
        if *self.shown.borrow() == machines {
            return;
        }
        while let Some(c) = self.list.first_child() {
            self.list.remove(&c);
        }
        for m in &machines {
            let row = adw::ActionRow::builder().title(&m.host).subtitle(&m.ip).use_markup(false).activatable(true).build();
            row.add_suffix(&gtk::Image::from_icon_name("go-next-symbolic"));
            let helper = self.clone();
            let machine = m.clone();
            row.connect_activated(move |_| helper.talk_to(&machine));
            self.list.append(&row);
        }
        self.list.set_visible(!machines.is_empty());
        *self.shown.borrow_mut() = machines;
    }

    fn talk_to(self: &Rc<Self>, machine: &Machine) {
        self.close_conversation();
        let view = ChatView::new(From::Helper, &machine.host, "Écrire à la personne dépannée…");
        view.set_sendable(false);
        view.set_status(Some("Connexion à l'ordinateur…"));
        let conv = Rc::new(Conversation {
            machine: machine.clone(),
            view: view.clone(),
            toasts: self.toasts.clone(),
            open: Cell::new(true),
            follower: RefCell::new(None),
        });
        {
            let conv = conv.clone();
            view.on_send(move |text| conv.send(text));
        }
        self.nav.push(&with_header(&machine.host, &view.root));
        view.focus();
        glib::spawn_future_local(conv.clone().receive());
        *self.conversation.borrow_mut() = Some(conv);
    }
}
