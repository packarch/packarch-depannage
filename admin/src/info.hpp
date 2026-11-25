// Infos utiles d'un ordinateur dépanné (système, session, matériel, disques,
// réseau) : un script qui ne fait que lire (data/infos.sh), lancé en root par
// Tailscale SSH. Il vient de Packarch Admin, pas de l'ordinateur dépanné : il
// fonctionne avec toutes les versions de packarch-depannage.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace info {

struct Row {
    QString label;
    QString value;
    bool operator==(const Row&) const = default;
};
struct Section {
    QString title;
    QList<Row> rows;
    bool operator==(const Section&) const = default;
};

/// Sections de la sortie du script. Elle vient de l'autre ordinateur : bornée,
/// sans caractères de contrôle, et seulement du texte.
QList<Section> parse(const QByteArray& output);

/// En texte, libellés alignés (pour le presse-papiers)
QString toText(const QList<Section>& sections);

}  // namespace info
