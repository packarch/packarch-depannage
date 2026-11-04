//! Fenêtre : un bouton pour autoriser le dépannage, puis le lien à
//! transmettre, l'état de la connexion, la fin automatique, « +1 h » et
//! « Arrêter maintenant », et le tchat une fois l'ordinateur connecté. L'état est
//! relu toutes les deux secondes, le tchat toutes les secondes. La personne qui
//! dépanne passe par « Dépanner quelqu'un » (helper.rs).

use crate::chat::{self, From};
use crate::chat_view::ChatView;
use crate::helper::Helper;
use crate::session::{self, Status};
use adw::prelude::*;
use gtk::{gdk, gio, glib};
use std::cell::{Cell, RefCell};
use std::ffi::OsStr;
use std::rc::Rc;

const APP_ID: &str = "org.packarch.Depannage";
const BIN: &str = "/usr/bin/packarch-depannage";
/// Durée d'une session et d'une prolongation (minutes)
const MINUTES: &str = "60";

struct Ui {
    toasts: adw::ToastOverlay,
    page: adw::StatusPage,
    start: gtk::Button,
    link_box: gtk::Box,
    link: gtk::Label,
    details: gtk::Label,
    active_buttons: gtk::Box,
    help_someone: gtk::Button,
    busy: RefCell<bool>,
    chat: ChatView,
    /// Position de lecture du tchat ; historique déjà affiché
    chat_pos: Cell<u64>,
    chat_loaded: Cell<bool>,
}

pub fn local_time(unix: u64) -> String {
    glib::DateTime::from_unix_local(unix as i64)
        .and_then(|d| d.format("%H h %M"))
        .map(|s| s.to_string())
        .unwrap_or_default()
}

impl Ui {
    fn toast(&self, msg: &str) {
        self.toasts.add_toast(adw::Toast::new(msg));
    }

    /// Relit l'état (le compte à rebours change à chaque minute)
    fn refresh(&self) {
        let st = session::status();
        self.show(&st);
    }

    fn show(&self, st: &Status) {
        let busy = *self.busy.borrow();
        self.start.set_visible(!st.active);
        self.start.set_sensitive(!busy);
        self.active_buttons.set_visible(st.active);
        self.active_buttons.set_sensitive(!busy);
        self.help_someone.set_visible(!st.active);
        self.show_chat(st.active && st.backend == "Running");
        if let Some(name) = &st.helper_name {
            self.chat.set_other(name);
        }
        if !st.active {
            self.page.set_icon_name(Some("preferences-desktop-remote-desktop"));
            self.page.set_title("Dépannage à distance");
            self.page.set_description(Some(
                "Autorise une personne de confiance à se connecter à cet ordinateur pour le réparer. \
                 Elle aura un accès complet, pendant une heure seulement : l'accès se coupe tout seul \
                 à la fin, ou dès que vous cliquez sur « Arrêter ».",
            ));
            self.link_box.set_visible(false);
            self.details.set_visible(false);
            return;
        }
        let end = st.end.map(|e| {
            let left = e.saturating_sub(session::now()).div_ceil(60);
            format!("Fin automatique à {} (dans {left} min).", local_time(e))
        });
        let machine = st.dns_name.as_deref().unwrap_or(&st.hostname);
        let (icon, title, text) = if st.helping() {
            // « : Fabrice (fab@example.com), depuis « archlinux » »
            let account = match (&st.helper_name, &st.helper_login) {
                (Some(n), Some(l)) if n != l => format!("{n} ({l})"),
                (Some(n), _) => n.clone(),
                (None, Some(l)) => l.clone(),
                (None, None) => String::new(),
            };
            let host = st.helper.as_deref().map(|h| format!("depuis « {h} »")).unwrap_or_default();
            let who = match (account.is_empty(), host.is_empty()) {
                (false, false) => format!(" : {account}, {host}"),
                (false, true) => format!(" : {account}"),
                (true, false) => format!(" ({host})"),
                (true, true) => String::new(),
            };
            let ago = st.last_activity.map(|t| session::now().saturating_sub(t) / 60);
            let when = match ago {
                Some(0) | None => "en ce moment".to_string(),
                Some(1) => "il y a 1 min".to_string(),
                Some(m) => format!("il y a {m} min"),
            };
            ("emblem-ok-symbolic", "Dépannage en cours", format!("La personne qui vous dépanne travaille sur cet ordinateur{who}. Dernière activité : {when}."))
        } else if st.backend == "Running" {
            ("network-transmit-receive-symbolic", "Prêt pour le dépannage", "L'ordinateur est joignable. La personne qui vous dépanne peut se connecter.".to_string())
        } else if st.auth_url.is_some() {
            ("mail-send-symbolic", "Envoyez ce lien", "Envoyez ce lien à la personne qui vous dépanne (SMS, e-mail…). Elle doit l'ouvrir pour se connecter.".to_string())
        } else {
            ("content-loading-symbolic", "Préparation…", "Démarrage de la connexion sécurisée.".to_string())
        };
        self.page.set_icon_name(Some(icon));
        self.page.set_title(title);
        self.page.set_description(Some(&text));
        match &st.auth_url {
            Some(url) if st.backend != "Running" => {
                self.link.set_label(url);
                self.link_box.set_visible(true);
            }
            _ => self.link_box.set_visible(false),
        }
        self.details.set_label(&format!("Nom de l'ordinateur : {machine}\n{}", end.unwrap_or_default()));
        self.details.set_visible(true);
    }

    /// Le tchat prend la place libre sous l'état, seulement pendant la session
    fn show_chat(&self, on: bool) {
        self.chat.root.set_visible(on);
        self.page.set_vexpand(!on);
        if on {
            self.page.add_css_class("compact");
        } else {
            self.page.remove_css_class("compact");
            self.chat.clear();
            self.chat_pos.set(0);
            self.chat_loaded.set(false);
        }
    }

    /// Affiche les nouveaux messages
    fn poll_chat(&self) {
        if !self.chat.root.is_visible() {
            return;
        }
        let live = self.chat_loaded.replace(true);
        let mut pos = self.chat_pos.get();
        for m in chat::read_new(chat::CHAT, &mut pos) {
            self.chat.add(&m, live);
        }
        self.chat_pos.set(pos);
    }

    /// pkexec packarch-depannage ARGS…, puis relecture de l'état
    fn privileged(self: &Rc<Self>, args: &[&str], failure: &'static str) {
        let mut argv: Vec<&OsStr> = vec![OsStr::new("pkexec"), OsStr::new(BIN)];
        argv.extend(args.iter().map(OsStr::new));
        // stderr lu : la raison de l'échec est affichée, pas seulement FAILURE
        let proc = match gio::Subprocess::newv(&argv, gio::SubprocessFlags::STDERR_PIPE) {
            Ok(p) => p,
            Err(e) => {
                self.toast(&format!("{failure} : {e}"));
                return;
            }
        };
        *self.busy.borrow_mut() = true;
        self.refresh();
        let ui = self.clone();
        glib::spawn_future_local(async move {
            let stderr = match proc.communicate_utf8_future(None).await {
                Ok((_, err)) => err.map(|s| s.to_string()).unwrap_or_default(),
                Err(e) => e.to_string(),
            };
            *ui.busy.borrow_mut() = false;
            if !proc.is_successful() {
                let reason = failure_reason(&proc, &stderr);
                eprintln!("{failure} : {reason}");
                ui.toasts.add_toast(adw::Toast::builder().title(format!("{failure} : {reason}")).timeout(15).build());
            }
            ui.refresh();
        });
    }
}

/// Raison de l'échec de « pkexec packarch-depannage … » : la dernière ligne
/// d'erreur du programme, ou le code de sortie de pkexec
fn failure_reason(proc: &gio::Subprocess, stderr: &str) -> String {
    if let Some(line) = stderr.lines().map(str::trim).filter(|l| !l.is_empty()).last() {
        return line.strip_prefix("packarch-depannage : ").unwrap_or(line).to_string();
    }
    if !proc.has_exited() {
        return format!("arrêté par le signal {}", proc.term_sig());
    }
    match proc.exit_status() {
        126 => "autorisation refusée ou annulée".into(),
        127 => "authentification impossible (agent polkit absent ?)".into(),
        code => format!("code de sortie {code}"),
    }
}

fn build_ui(app: &adw::Application) {
    // Déjà ouverte (relancée par un message reçu, ou depuis le menu)
    if let Some(window) = app.active_window() {
        window.present();
        return;
    }
    let start = gtk::Button::with_label("Autoriser le dépannage (1 h)");
    start.add_css_class("pill");
    start.add_css_class("suggested-action");
    start.set_halign(gtk::Align::Center);

    let link = gtk::Label::builder().selectable(true).wrap(true).wrap_mode(gtk::pango::WrapMode::Char).build();
    link.add_css_class("title-4");
    link.add_css_class("monospace");
    let copy = gtk::Button::with_label("Copier le lien");
    copy.add_css_class("pill");
    copy.set_halign(gtk::Align::Center);
    let link_box = gtk::Box::new(gtk::Orientation::Vertical, 12);
    link_box.append(&link);
    link_box.append(&copy);

    let details = gtk::Label::builder().justify(gtk::Justification::Center).wrap(true).build();
    details.add_css_class("dim-label");

    let more = gtk::Button::with_label("+1 h");
    more.add_css_class("pill");
    let stop = gtk::Button::with_label("Arrêter maintenant");
    stop.add_css_class("pill");
    stop.add_css_class("destructive-action");
    let active_buttons = gtk::Box::new(gtk::Orientation::Horizontal, 12);
    active_buttons.set_halign(gtk::Align::Center);
    active_buttons.append(&more);
    active_buttons.append(&stop);

    let content = gtk::Box::new(gtk::Orientation::Vertical, 18);
    content.set_halign(gtk::Align::Center);
    content.append(&link_box);
    content.append(&details);
    content.append(&start);
    content.append(&active_buttons);
    let help_someone = gtk::Button::with_label("Dépanner quelqu'un");
    help_someone.add_css_class("pill");
    help_someone.set_halign(gtk::Align::Center);
    content.append(&help_someone);

    let page = adw::StatusPage::builder().child(&content).vexpand(true).build();
    let chat_view = ChatView::new(From::User, "La personne qui vous dépanne", "Écrire à la personne qui vous dépanne…");
    let main = gtk::Box::new(gtk::Orientation::Vertical, 0);
    main.append(&page);
    main.append(&chat_view.root);
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&main));

    let nav = adw::NavigationView::new();
    nav.add(&adw::NavigationPage::new(&toolbar, "Dépannage à distance"));
    let toasts = adw::ToastOverlay::new();
    toasts.set_child(Some(&nav));
    let helper = Helper::new(&nav, &toasts);

    let window = adw::ApplicationWindow::builder()
        .application(app)
        .title("Dépannage à distance")
        .default_width(520)
        .default_height(720)
        .content(&toasts)
        .build();

    let ui = Rc::new(Ui {
        toasts,
        page,
        start: start.clone(),
        link_box,
        link: link.clone(),
        details,
        active_buttons,
        help_someone: help_someone.clone(),
        busy: RefCell::new(false),
        chat: chat_view.clone(),
        chat_pos: Cell::new(0),
        chat_loaded: Cell::new(false),
    });

    {
        let ui = ui.clone();
        chat_view.on_send(move |text| {
            match chat::append_to(chat::CHAT, From::User, &text) {
                Ok(()) => ui.poll_chat(),
                Err(e) => {
                    ui.chat.restore(&text);
                    ui.toast(&format!("Message non envoyé : {e}"));
                }
            }
        });
    }
    {
        let helper = helper.clone();
        help_someone.connect_clicked(move |_| helper.open());
    }
    {
        let helper = helper.clone();
        window.connect_close_request(move |_| {
            helper.close_conversation();
            glib::Propagation::Proceed
        });
    }

    {
        let ui = ui.clone();
        start.connect_clicked(move |_| ui.privileged(&["start", MINUTES], "Le dépannage n'a pas été autorisé"));
    }
    {
        let ui = ui.clone();
        more.connect_clicked(move |_| ui.privileged(&["extend", MINUTES], "La session n'a pas été prolongée"));
    }
    {
        let ui = ui.clone();
        stop.connect_clicked(move |_| ui.privileged(&["stop"], "La session n'a pas pu être arrêtée"));
    }
    {
        let ui = ui.clone();
        copy.connect_clicked(move |_| {
            if let Some(d) = gdk::Display::default() {
                d.clipboard().set_text(&ui.link.label());
                ui.toast("Lien copié");
            }
        });
    }

    ui.refresh();
    ui.poll_chat();
    {
        let ui = ui.clone();
        glib::timeout_add_seconds_local(2, move || {
            ui.refresh();
            helper.tick();
            glib::ControlFlow::Continue
        });
    }
    {
        let ui = ui.clone();
        glib::timeout_add_seconds_local(1, move || {
            ui.poll_chat();
            glib::ControlFlow::Continue
        });
    }
    window.present();
}

pub fn run() -> glib::ExitCode {
    let app = adw::Application::builder().application_id(APP_ID).build();
    app.connect_activate(build_ui);
    // Les arguments sont ceux de la ligne de commande (traités avant)
    app.run_with_args::<&str>(&[])
}
