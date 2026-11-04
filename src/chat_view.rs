//! Zone de tchat, la même des deux côtés : les messages, un champ de saisie.

use crate::chat::{From, Message};
use crate::ui::local_time;
use adw::prelude::*;
use gtk::{gio, glib};
use std::cell::RefCell;
use std::rc::Rc;

/// Titre et texte d'une notification : ceux de Packarch (dunst, thèmes Openbox)
/// ont une hauteur fixe de deux lignes d'environ 38 caractères, le reste est coupé
const NOTIFICATION_TITLE: &str = "Dépannage : message reçu";
const NOTIFICATION_CHARS: usize = 36;

/// Début du message, sur une ligne, en texte échappé (le texte des notifications
/// est interprété comme du balisage)
fn notification_body(text: &str) -> String {
    let line = text.split_whitespace().collect::<Vec<_>>().join(" ");
    let mut short: String = line.chars().take(NOTIFICATION_CHARS).collect();
    if line.chars().count() > NOTIFICATION_CHARS {
        short.push('…');
    }
    glib::markup_escape_text(&short).to_string()
}

#[derive(Clone)]
pub struct ChatView {
    pub root: gtk::Box,
    banner: adw::Banner,
    list: gtk::Box,
    entry: gtk::Entry,
    send: gtk::Button,
    me: From,
    /// Nom affiché pour l'autre personne
    other: Rc<RefCell<String>>,
}

impl ChatView {
    pub fn new(me: From, other: &str, placeholder: &str) -> Self {
        let banner = adw::Banner::new("");
        let list = gtk::Box::new(gtk::Orientation::Vertical, 8);
        list.set_valign(gtk::Align::End);
        list.set_margin_top(8);
        list.set_margin_bottom(8);
        list.set_margin_start(12);
        list.set_margin_end(12);
        let scroller = gtk::ScrolledWindow::builder()
            .hscrollbar_policy(gtk::PolicyType::Never)
            .vexpand(true)
            .min_content_height(160)
            .child(&list)
            .build();
        // Toujours sur le dernier message
        scroller.vadjustment().connect_upper_notify(|a| a.set_value(a.upper() - a.page_size()));

        let entry = gtk::Entry::builder().hexpand(true).placeholder_text(placeholder).build();
        let send = gtk::Button::with_label("Envoyer");
        send.add_css_class("suggested-action");
        let input = gtk::Box::new(gtk::Orientation::Horizontal, 8);
        input.set_margin_top(8);
        input.set_margin_bottom(12);
        input.set_margin_start(12);
        input.set_margin_end(12);
        input.append(&entry);
        input.append(&send);

        let root = gtk::Box::new(gtk::Orientation::Vertical, 0);
        root.set_vexpand(true);
        root.append(&gtk::Separator::new(gtk::Orientation::Horizontal));
        root.append(&banner);
        root.append(&scroller);
        root.append(&input);
        Self { root, banner, list, entry, send, me, other: Rc::new(RefCell::new(other.to_string())) }
    }

    /// F reçoit le texte à envoyer (champ vidé)
    pub fn on_send(&self, f: impl Fn(String) + 'static) {
        let f = Rc::new(f);
        let submit = {
            let entry = self.entry.clone();
            move || {
                let text = entry.text().trim().to_string();
                if !text.is_empty() {
                    entry.set_text("");
                    f(text);
                }
            }
        };
        let s = submit.clone();
        self.entry.connect_activate(move |_| s());
        self.send.connect_clicked(move |_| submit());
    }

    pub fn set_other(&self, name: &str) {
        *self.other.borrow_mut() = name.to_string();
    }

    /// Bandeau d'état de la connexion (None : caché)
    pub fn set_status(&self, text: Option<&str>) {
        self.banner.set_title(text.unwrap_or(""));
        self.banner.set_revealed(text.is_some());
    }

    pub fn set_sendable(&self, on: bool) {
        self.entry.set_sensitive(on);
        self.send.set_sensitive(on);
    }

    /// Remet un texte qui n'a pas pu partir, sans écraser une saisie en cours
    pub fn restore(&self, text: &str) {
        if self.entry.text().is_empty() {
            self.entry.set_text(text);
        }
    }

    pub fn focus(&self) {
        self.entry.grab_focus();
    }

    pub fn clear(&self) {
        while let Some(c) = self.list.first_child() {
            self.list.remove(&c);
        }
    }

    /// Affiche un message. LIVE : il vient d'arriver (pas l'historique), à
    /// signaler si la fenêtre n'est pas au premier plan.
    pub fn add(&self, m: &Message, live: bool) {
        let mine = m.from == self.me;
        let who = if mine { "Vous".to_string() } else { self.other.borrow().clone() };
        let meta = gtk::Label::new(Some(&format!("{who} · {}", local_time(m.time))));
        meta.add_css_class("caption");
        meta.add_css_class("dim-label");
        meta.set_xalign(0.0);
        // Label::new : texte brut, sans balisage
        let text = gtk::Label::new(Some(&m.text));
        text.set_wrap(true);
        text.set_wrap_mode(gtk::pango::WrapMode::WordChar);
        text.set_selectable(true);
        text.set_xalign(0.0);
        text.set_max_width_chars(44);
        let bubble = gtk::Box::new(gtk::Orientation::Vertical, 2);
        bubble.add_css_class("card");
        bubble.set_halign(if mine { gtk::Align::End } else { gtk::Align::Start });
        let inner = gtk::Box::new(gtk::Orientation::Vertical, 2);
        inner.set_margin_top(6);
        inner.set_margin_bottom(6);
        inner.set_margin_start(10);
        inner.set_margin_end(10);
        inner.append(&meta);
        inner.append(&text);
        bubble.append(&inner);
        self.list.append(&bubble);
        if live && !mine {
            self.notify(&m.text);
        }
    }

    fn notify(&self, text: &str) {
        let Some(window) = self.root.root().and_downcast::<gtk::Window>() else { return };
        if window.is_active() {
            return;
        }
        if let Some(app) = window.application() {
            let n = gio::Notification::new(NOTIFICATION_TITLE);
            n.set_body(Some(&notification_body(text)));
            app.send_notification(Some("chat"), &n);
        }
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
}
