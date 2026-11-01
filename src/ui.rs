//! Fenêtre : un bouton pour autoriser le dépannage, puis le lien à
//! transmettre, l'état de la connexion, la fin automatique, « +1 h » et
//! « Arrêter maintenant ». L'état est relu toutes les deux secondes.

use crate::session::{self, Status};
use adw::prelude::*;
use gtk::{gdk, gio, glib};
use std::cell::RefCell;
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
    busy: RefCell<bool>,
}

fn local_time(unix: u64) -> String {
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
        let (icon, title, text) = if st.ssh_sessions > 0 {
            ("emblem-ok-symbolic", "Dépannage en cours", "La personne qui vous dépanne est connectée à cet ordinateur.".to_string())
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

    /// pkexec packarch-depannage ARGS…, puis relecture de l'état
    fn privileged(self: &Rc<Self>, args: &[&str], failure: &'static str) {
        let mut argv: Vec<&OsStr> = vec![OsStr::new("pkexec"), OsStr::new(BIN)];
        argv.extend(args.iter().map(OsStr::new));
        let proc = match gio::Subprocess::newv(&argv, gio::SubprocessFlags::NONE) {
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
            let ok = proc.wait_check_future().await.is_ok();
            *ui.busy.borrow_mut() = false;
            if !ok {
                ui.toast(failure);
            }
            ui.refresh();
        });
    }
}

fn build_ui(app: &adw::Application) {
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

    let page = adw::StatusPage::builder().child(&content).build();
    let toasts = adw::ToastOverlay::new();
    toasts.set_child(Some(&page));
    let toolbar = adw::ToolbarView::new();
    toolbar.add_top_bar(&adw::HeaderBar::new());
    toolbar.set_content(Some(&toasts));

    let window = adw::ApplicationWindow::builder()
        .application(app)
        .title("Dépannage à distance")
        .default_width(520)
        .default_height(560)
        .content(&toolbar)
        .build();

    let ui = Rc::new(Ui {
        toasts,
        page,
        start: start.clone(),
        link_box,
        link: link.clone(),
        details,
        active_buttons,
        busy: RefCell::new(false),
    });

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
    {
        let ui = ui.clone();
        glib::timeout_add_seconds_local(2, move || {
            ui.refresh();
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
