// Affichage du tchat, le même des deux côtés : une ligne par message, le nom de
// celui qui écrit en couleur, puis le texte, puis l'heure, discrète.
#pragma once

#include "chatmsg.hpp"

#include <QTextBrowser>

class ChatView : public QTextBrowser {
public:
    /// Qui écrit : sa couleur
    enum class Role { Person, Helper, Assistant };

    explicit ChatView(QWidget* parent = nullptr);
    /// Ajoute MESSAGE sous le nom NAME (son texte reste du texte : aucun balisage n'en est interprété)
    void addMessage(const ChatMessage& message, const QString& name);

    /// Rôle de l'auteur d'un message (l'assistant écrit sous le nom « Claude »)
    static Role roleOf(const ChatMessage& message);
    /// La ligne d'un message, en HTML (le nom et le texte y sont échappés)
    static QString html(const QString& name, const QString& text, const QString& time, Role role);
};
