//! Fenêtre (egui) : autoriser le dépannage,
//! le lien à transmettre, l'état, le tchat ; « Dépanner quelqu'un » pour l'autre
//! côté. Rien n'attend ici (voir worker.rs) : seul le dessin de chaque image.

use crate::chat::{self, From, Message};
use crate::session::{self, Machine, Status};
use crate::worker::{self, Bus, Conversation, ConversationEvent, Event};
use chrono::TimeZone;
use eframe::egui::{self, Align, Color32, Layout, RichText};
use std::fs::OpenOptions;
use std::process::{Command, ExitCode};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::sync::mpsc::{Receiver, channel};
use std::time::{Duration, Instant};

const MINUTES: &str = "60";
const TITLE: &str = "Dépannage à distance";
const ACCENT: Color32 = Color32::from_rgb(53, 132, 228);
const DANGER: Color32 = Color32::from_rgb(192, 28, 40);
const TOAST_TIME: Duration = Duration::from_secs(10);

fn local_time(unix: u64) -> String {
    chrono::Local.timestamp_opt(unix as i64, 0).single().map(|d| d.format("%H h %M").to_string()).unwrap_or_default()
}

/// Icônes dessinées, en lignes dans un carré de côté 1 : les polices d'egui n'ont
/// pas ces symboles, et la fenêtre ne dépend pas du thème d'icônes
type Icon = &'static [&'static [(f32, f32)]];
const MONITOR: Icon = &[&[(0.0, 0.1), (1.0, 0.1), (1.0, 0.7), (0.0, 0.7), (0.0, 0.1)], &[(0.5, 0.7), (0.5, 0.9)], &[(0.25, 0.9), (0.75, 0.9)]];
const CHECK: Icon = &[&[(0.1, 0.55), (0.4, 0.85), (0.9, 0.2)]];
const ARROWS: Icon = &[&[(0.1, 0.35), (0.9, 0.35), (0.7, 0.15)], &[(0.9, 0.65), (0.1, 0.65), (0.3, 0.85)]];
const ENVELOPE: Icon = &[&[(0.0, 0.2), (1.0, 0.2), (1.0, 0.8), (0.0, 0.8), (0.0, 0.2)], &[(0.0, 0.2), (0.5, 0.55), (1.0, 0.2)]];
const HOURGLASS: Icon = &[&[(0.2, 0.05), (0.8, 0.05), (0.2, 0.95), (0.8, 0.95), (0.2, 0.05)]];

fn draw_icon(ui: &mut egui::Ui, icon: Icon, size: f32) {
    let (rect, _) = ui.allocate_exact_size(egui::vec2(size, size), egui::Sense::hover());
    let stroke = egui::Stroke::new((size / 14.0).max(2.0), Color32::from_gray(170));
    for line in icon {
        for pair in line.windows(2) {
            let point = |(x, y): (f32, f32)| rect.min + egui::vec2(x * size, y * size);
            ui.painter().line_segment([point(pair[0]), point(pair[1])], stroke);
        }
    }
}

/// Icône, titre et texte de l'état de la session
fn describe(st: &Status) -> (Icon, &'static str, String) {
    if !st.active {
        return (
            MONITOR,
            TITLE,
            "Autorise une personne de confiance à se connecter à cet ordinateur pour le réparer. \
             Elle aura un accès complet, pendant une heure seulement : l'accès se coupe tout seul \
             à la fin, ou dès que vous cliquez sur « Arrêter »."
                .to_string(),
        );
    }
    if st.helping() {
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
        let when = match st.last_activity.map(|t| session::now().saturating_sub(t) / 60) {
            Some(0) | None => "en ce moment".to_string(),
            Some(1) => "il y a 1 min".to_string(),
            Some(m) => format!("il y a {m} min"),
        };
        return (CHECK, "Dépannage en cours", format!("La personne qui vous dépanne travaille sur cet ordinateur{who}. Dernière activité : {when}."));
    }
    if st.backend == "Running" {
        return (ARROWS, "Prêt pour le dépannage", "L'ordinateur est joignable. La personne qui vous dépanne peut se connecter.".to_string());
    }
    if st.auth_url.is_some() {
        return (ENVELOPE, "Envoyez ce lien", "Envoyez ce lien à la personne qui vous dépanne (SMS, e-mail…). Elle doit l'ouvrir pour se connecter.".to_string());
    }
    (HOURGLASS, "Préparation…", "Démarrage de la connexion sécurisée.".to_string())
}

/// Une zone de tchat
#[derive(Default)]
struct ChatLog {
    messages: Vec<Message>,
    input: String,
    /// Remettre le curseur dans le champ à l'image suivante
    focus: bool,
}

/// Bouton mis en avant, bien visible
fn big_button(ui: &mut egui::Ui, text: &str, fill: Option<Color32>, enabled: bool) -> bool {
    let mut label = RichText::new(text).size(15.0);
    if fill.is_some() {
        label = label.color(Color32::WHITE);
    }
    let mut button = egui::Button::new(label).min_size(egui::vec2(0.0, 36.0)).corner_radius(18.0);
    if let Some(c) = fill {
        button = button.fill(c);
    }
    ui.add_enabled(enabled, button).clicked()
}

/// Barre du haut : titre, version, retour éventuel. Vrai si « retour » est cliqué.
fn header(ui: &mut egui::Ui, title: &str, subtitle: Option<&str>, back: bool) -> bool {
    let mut clicked = false;
    egui::Panel::top("header").show(ui, |ui| {
        ui.add_space(6.0);
        ui.horizontal(|ui| {
            let width = ui.available_width();
            if back {
                clicked = ui.button("Retour").clicked();
            }
            let used = width - ui.available_width();
            ui.add_space((width / 2.0 - used - 130.0).max(0.0));
            ui.allocate_ui_with_layout(egui::vec2(260.0, 34.0), Layout::top_down(Align::Center), |ui| {
                ui.label(RichText::new(title).strong());
                if let Some(s) = subtitle {
                    ui.label(RichText::new(s).small().weak());
                }
            });
        });
        ui.add_space(6.0);
    });
    clicked
}

/// Tchat : bandeau éventuel, champ de saisie en bas, messages au milieu. Rend le
/// texte à envoyer.
fn chat_panels(ui: &mut egui::Ui, log: &mut ChatLog, me: From, other: &str, placeholder: &str, banner: Option<&str>, sendable: bool) -> Option<String> {
    if let Some(text) = banner {
        egui::Panel::top("banner").frame(egui::Frame::new().fill(Color32::from_gray(70)).inner_margin(8.0)).show(ui, |ui| {
            ui.vertical_centered(|ui| ui.label(RichText::new(text).strong().color(Color32::WHITE)));
        });
    }
    let mut to_send = None;
    egui::Panel::bottom("input").show(ui, |ui| {
        ui.add_space(8.0);
        ui.add_enabled_ui(sendable, |ui| {
            ui.horizontal(|ui| {
                let button_width = 90.0;
                let edit = egui::TextEdit::singleline(&mut log.input)
                    .hint_text(placeholder)
                    .desired_width(ui.available_width() - button_width)
                    .margin(egui::vec2(8.0, 7.0));
                let response = ui.add(edit);
                if log.focus && sendable {
                    response.request_focus();
                    log.focus = false;
                }
                let enter = response.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
                let clicked = ui.add_sized([button_width - 8.0, 32.0], egui::Button::new(RichText::new("Envoyer").color(Color32::WHITE)).fill(ACCENT)).clicked();
                if (enter || clicked) && !log.input.trim().is_empty() {
                    to_send = Some(log.input.trim().to_string());
                    log.input.clear();
                    log.focus = true;
                }
            });
        });
        ui.add_space(8.0);
    });
    egui::CentralPanel::default().show(ui, |ui| {
        egui::ScrollArea::vertical().auto_shrink(false).stick_to_bottom(true).show(ui, |ui| {
            let max_width = ui.available_width() * 0.8;
            for m in &log.messages {
                let mine = m.from == me;
                let align = if mine { Align::Max } else { Align::Min };
                ui.with_layout(Layout::top_down(align), |ui| {
                    egui::Frame::new().fill(Color32::from_gray(50)).corner_radius(10.0).inner_margin(egui::vec2(10.0, 6.0)).show(ui, |ui| {
                        ui.set_max_width(max_width);
                        ui.with_layout(Layout::top_down(Align::Min), |ui| {
                            let who = if mine { "Vous" } else { other };
                            ui.label(RichText::new(format!("{who} · {}", local_time(m.time))).small().weak());
                            // Du texte, jamais de balisage
                            ui.add(egui::Label::new(RichText::new(m.text.as_str()).color(Color32::from_gray(235))).wrap());
                        });
                    });
                });
                ui.add_space(6.0);
            }
        });
    });
    to_send
}

enum Screen {
    Main,
    Machines,
    Conversation,
}

struct ConversationView {
    conversation: Conversation,
    log: ChatLog,
    banner: Option<String>,
    sendable: bool,
}

struct App {
    bus: Bus,
    rx: Receiver<Event>,
    version: String,
    screen: Screen,
    status: Status,
    busy: bool,
    toasts: Vec<(String, Instant)>,
    log: ChatLog,
    machines: Result<Vec<Machine>, String>,
    view: Option<ConversationView>,
    next_id: u64,
    /// Fermeture demandée pendant une session : confirmation affichée
    confirm_close: bool,
    /// Fermeture confirmée : ne plus l'intercepter
    close_confirmed: bool,
}

impl App {
    fn new(cc: &eframe::CreationContext) -> Self {
        cc.egui_ctx.set_visuals(egui::Visuals::dark());
        cc.egui_ctx.set_zoom_factor(1.15);
        let (tx, rx) = channel();
        let bus = Bus {
            tx,
            ctx: cc.egui_ctx.clone(),
            focused: Arc::new(AtomicBool::new(true)),
            want_machines: Arc::new(AtomicBool::new(false)),
        };
        worker::watch_status(bus.clone());
        worker::watch_user_chat(bus.clone());
        Self {
            bus,
            rx,
            version: format!("Version {}", env!("CARGO_PKG_VERSION")),
            screen: Screen::Main,
            status: Status { hostname: session::hostname(), ..Default::default() },
            busy: false,
            toasts: Vec::new(),
            log: ChatLog { focus: true, ..Default::default() },
            machines: Ok(Vec::new()),
            view: None,
            next_id: 0,
            confirm_close: false,
            close_confirmed: false,
        }
    }

    fn toast(&mut self, text: String) {
        self.toasts.push((text, Instant::now()));
    }

    fn handle(&mut self, event: Event) {
        match event {
            Event::Status(st) => self.status = st,
            Event::Machines(m) => self.machines = m,
            Event::UserMessage(m) => self.log.messages.push(m),
            Event::UserChatReset => self.log.messages.clear(),
            Event::Privileged(result, failure) => {
                self.busy = false;
                if let Err(reason) = result {
                    self.toast(format!("{failure} : {reason}"));
                }
            }
            Event::Conversation(id, event) => {
                let Some(view) = self.view.as_mut().filter(|v| v.conversation.id == id) else { return };
                match event {
                    ConversationEvent::Connected => {
                        if !view.sendable {
                            view.log.focus = true;
                        }
                        view.banner = None;
                        view.sendable = true;
                    }
                    ConversationEvent::Message(m) => view.log.messages.push(m),
                    ConversationEvent::Lost(error) => {
                        view.banner = Some(format!("Connexion perdue, nouvel essai… {error}"));
                        view.sendable = false;
                    }
                    ConversationEvent::Finished(text) => {
                        view.banner = Some(text);
                        view.sendable = false;
                    }
                    ConversationEvent::NotSent(text, reason) => {
                        if view.log.input.is_empty() {
                            view.log.input = text;
                        }
                        self.toast(format!("Message non envoyé : {reason}"));
                    }
                }
            }
        }
    }

    fn close_conversation(&mut self) {
        if let Some(view) = self.view.take() {
            view.conversation.close();
        }
    }

    fn show_machines(&mut self) {
        self.screen = Screen::Machines;
        self.bus.want_machines.store(true, Ordering::Relaxed);
        worker::refresh_machines(self.bus.clone());
    }

    fn talk_to(&mut self, machine: Machine) {
        self.close_conversation();
        self.next_id += 1;
        self.view = Some(ConversationView {
            conversation: Conversation::start(self.next_id, machine, self.bus.clone()),
            log: ChatLog::default(),
            banner: Some("Connexion à l'ordinateur…".into()),
            sendable: false,
        });
        self.screen = Screen::Conversation;
        self.bus.want_machines.store(false, Ordering::Relaxed);
    }

    fn privileged(&mut self, args: Vec<&'static str>, failure: &'static str) {
        self.busy = true;
        worker::privileged(self.bus.clone(), args, failure);
    }

    /// Écran principal : état, lien, boutons, et le tchat une fois connecté
    fn main_screen(&mut self, ui: &mut egui::Ui) {
        let st = self.status.clone();
        header(ui, TITLE, Some(&self.version), false);
        let chat_on = st.active && st.backend == "Running";
        if chat_on {
            egui::Panel::top("status").show(ui, |ui| self.status_ui(ui, &st));
            let other = st.helper_name.clone().unwrap_or_else(|| "La personne qui vous dépanne".to_string());
            let sent = chat_panels(ui, &mut self.log, From::User, &other, "Écrire à la personne qui vous dépanne…", None, true);
            if let Some(text) = sent {
                if let Err(e) = chat::append_to(chat::CHAT, From::User, &text) {
                    self.log.input = text;
                    self.toast(format!("Message non envoyé : {e}"));
                }
            }
        } else {
            egui::CentralPanel::default().show(ui, |ui| {
                ui.add_space((ui.available_height() / 2.0 - 190.0).max(10.0));
                self.status_ui(ui, &st);
            });
        }
    }

    fn status_ui(&mut self, ui: &mut egui::Ui, st: &Status) {
        let (icon, title, text) = describe(st);
        ui.vertical_centered(|ui| {
            ui.add_space(8.0);
            draw_icon(ui, icon, if st.active { 40.0 } else { 72.0 });
            ui.add_space(4.0);
            ui.label(RichText::new(title).size(if st.active { 18.0 } else { 24.0 }).strong());
            ui.add_space(4.0);
            ui.label(text);
            ui.add_space(10.0);
            if !st.active {
                if big_button(ui, "Autoriser le dépannage (1 h)", Some(ACCENT), !self.busy) {
                    self.privileged(vec!["start", MINUTES], "Le dépannage n'a pas été autorisé");
                }
                ui.add_space(12.0);
                if big_button(ui, "Dépanner quelqu'un", None, true) {
                    self.show_machines();
                }
                return;
            }
            if let Some(url) = st.auth_url.as_ref().filter(|_| st.backend != "Running") {
                ui.add(egui::Label::new(RichText::new(url).monospace().size(15.0)).wrap());
                ui.add_space(8.0);
                if big_button(ui, "Copier le lien", None, true) {
                    ui.ctx().copy_text(url.clone());
                    self.toast("Lien copié".into());
                }
                ui.add_space(10.0);
            }
            let machine = st.dns_name.as_deref().unwrap_or(&st.hostname);
            ui.label(RichText::new(format!("Nom de l'ordinateur : {machine}")).weak());
            if let Some(e) = st.end {
                let left = e.saturating_sub(session::now()).div_ceil(60);
                ui.label(RichText::new(format!("Fin automatique à {} (dans {left} min).", local_time(e))).weak());
            }
            ui.add_space(10.0);
            // Deux boutons centrés : la largeur de la rangée, mesurée, sert à la centrer
            let row = 240.0;
            ui.allocate_ui_with_layout(egui::vec2(row, 38.0), Layout::left_to_right(Align::Center), |ui| {
                if big_button(ui, "+1 h", None, !self.busy) {
                    self.privileged(vec!["extend", MINUTES], "La session n'a pas été prolongée");
                }
                ui.add_space(8.0);
                if big_button(ui, "Arrêter maintenant", Some(DANGER), !self.busy) {
                    self.privileged(vec!["stop"], "La session n'a pas pu être arrêtée");
                }
            });
            ui.add_space(10.0);
        });
    }

    fn machines_screen(&mut self, ui: &mut egui::Ui) {
        if header(ui, "Dépanner quelqu'un", None, true) {
            self.screen = Screen::Main;
            self.bus.want_machines.store(false, Ordering::Relaxed);
        }
        let mut chosen = None;
        egui::CentralPanel::default().show(ui, |ui| {
            ui.add_space(40.0);
            ui.vertical_centered(|ui| match &self.machines {
                Err(e) => {
                    ui.label(RichText::new("Tailscale n'est pas connecté").size(22.0).strong());
                    ui.add_space(8.0);
                    ui.label(e);
                }
                Ok(list) if list.is_empty() => {
                    ui.label(RichText::new("Aucun ordinateur à dépanner").size(22.0).strong());
                    ui.add_space(8.0);
                    ui.label(
                        "La personne doit ouvrir « Dépannage à distance », cliquer sur « Autoriser le dépannage » \
                         et vous envoyer le lien. Ouvrez ce lien avec votre compte Tailscale : son ordinateur \
                         apparaîtra ici.",
                    );
                }
                Ok(list) => {
                    ui.label(RichText::new("Ordinateurs à dépanner").size(22.0).strong());
                    ui.add_space(8.0);
                    ui.label("Choisissez un ordinateur pour discuter avec la personne.");
                    ui.add_space(16.0);
                    for m in list {
                        let text = format!("{}   —   {}", m.host, m.ip);
                        if ui.add_sized([ui.available_width() * 0.85, 42.0], egui::Button::new(text)).clicked() {
                            chosen = Some(m.clone());
                        }
                        ui.add_space(6.0);
                    }
                }
            });
        });
        if let Some(m) = chosen {
            self.talk_to(m);
        }
    }

    fn conversation_screen(&mut self, ui: &mut egui::Ui) {
        let Some(view) = self.view.as_mut() else {
            self.screen = Screen::Machines;
            return;
        };
        let host = view.conversation.machine.host.clone();
        let back = header(ui, &host, None, true);
        let sent = chat_panels(ui, &mut view.log, From::Helper, &host, "Écrire à la personne dépannée…", view.banner.as_deref(), view.sendable);
        if let Some(text) = sent {
            view.conversation.send(text);
        }
        if back {
            self.close_conversation();
            self.show_machines();
        }
    }

    /// La session continue sans la fenêtre, qui porte le bouton pour l'arrêter :
    /// une fermeture par mégarde ne doit pas la faire oublier
    fn confirm_close_ui(&mut self, ctx: &egui::Context) {
        if ctx.input(|i| i.viewport().close_requested()) && self.status.active && !self.close_confirmed {
            ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
            self.confirm_close = true;
        }
        if !self.status.active {
            self.confirm_close = false;
        }
        if !self.confirm_close {
            return;
        }
        let end = self.status.end.map(local_time);
        let modal = egui::Modal::new(egui::Id::unique("confirm_close")).show(ctx, |ui| {
            ui.set_width(340.0);
            let mut keep = false;
            ui.vertical_centered(|ui| {
                ui.label(RichText::new("Fermer cette fenêtre ?").size(18.0).strong());
                ui.add_space(8.0);
                ui.label("Le dépannage continuera : la personne qui vous dépanne gardera l'accès à cet ordinateur.");
                ui.add_space(4.0);
                if let Some(end) = &end {
                    ui.label(format!("Il s'arrêtera tout seul à {end}."));
                }
                ui.label("Pour l'arrêter avant, rouvrez « Dépannage à distance » depuis le menu.");
                ui.add_space(12.0);
                keep = big_button(ui, "Laisser la fenêtre ouverte", Some(ACCENT), true);
                ui.add_space(8.0);
                if big_button(ui, "Fermer la fenêtre", None, true) {
                    self.close_confirmed = true;
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
            });
            keep
        });
        if modal.inner || modal.should_close() {
            self.confirm_close = false;
        }
    }

    fn toasts_ui(&mut self, ctx: &egui::Context) {
        self.toasts.retain(|(_, since)| since.elapsed() < TOAST_TIME);
        if self.toasts.is_empty() {
            return;
        }
        egui::Area::new(egui::Id::unique("toasts")).anchor(egui::Align2::CENTER_BOTTOM, [0.0, -70.0]).show(ctx, |ui| {
            for (text, _) in &self.toasts {
                egui::Frame::popup(ui.style()).show(ui, |ui| {
                    ui.set_max_width(420.0);
                    ui.add(egui::Label::new(text.as_str()).wrap());
                });
            }
        });
    }
}

impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        self.bus.focused.store(ctx.input(|i| i.focused), Ordering::Relaxed);
        while let Ok(event) = self.rx.try_recv() {
            self.handle(event);
        }
        match self.screen {
            Screen::Main => self.main_screen(ui),
            Screen::Machines => self.machines_screen(ui),
            Screen::Conversation => self.conversation_screen(ui),
        }
        self.toasts_ui(&ctx);
        self.confirm_close_ui(&ctx);
        // Le compte à rebours et la disparition des messages
        ctx.request_repaint_after(Duration::from_secs(1));
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        self.close_conversation();
    }
}

/// Une seule fenêtre : verrou tenu tant qu'elle est ouverte
fn single_instance() -> Option<std::fs::File> {
    let dir = std::env::var("XDG_RUNTIME_DIR").unwrap_or_else(|_| "/tmp".into());
    let file = OpenOptions::new().create(true).write(true).truncate(false).open(format!("{dir}/packarch-depannage.lock")).ok()?;
    file.try_lock().is_ok().then_some(file)
}

pub fn run() -> ExitCode {
    let Some(_lock) = single_instance() else {
        // Déjà ouverte (depuis le menu, le plus souvent) : la ramener devant
        let _ = Command::new("wmctrl").args(["-a", TITLE]).status();
        return ExitCode::SUCCESS;
    };
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_title(TITLE).with_app_id("org.packarch.Depannage").with_inner_size([520.0, 720.0]).with_min_inner_size([420.0, 500.0]),
        ..Default::default()
    };
    match eframe::run_native(TITLE, options, Box::new(|cc| Ok(Box::new(App::new(cc))))) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("packarch-depannage : {e}");
            ExitCode::FAILURE
        }
    }
}
