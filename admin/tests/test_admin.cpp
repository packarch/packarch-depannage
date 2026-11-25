#include "chat.hpp"
#include "info.hpp"
#include "keys.hpp"
#include "screenproto.hpp"
#include "tailnet.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace Qt::StringLiterals;

class TestAdmin : public QObject {
    Q_OBJECT
private slots:
    void machinesToHelpFiltered() {
        const QByteArray json = R"({"BackendState": "Running", "Peer": {
            "a": {"HostName": "packarch-papa", "Online": true, "Tags": ["tag:depannage"], "TailscaleIPs": ["100.1.1.2", "fd7a::2"]},
            "b": {"HostName": "packarch-maman", "Online": true, "Tags": ["tag:depannage"], "TailscaleIPs": ["100.1.1.1"]},
            "c": {"HostName": "packarch-parti", "Online": false, "Tags": ["tag:depannage"], "TailscaleIPs": ["100.1.1.3"]},
            "d": {"HostName": "minipc", "Online": true, "TailscaleIPs": ["100.1.1.4"]}
        }})";
        const QList<Machine> expected{{u"packarch-maman"_s, u"100.1.1.1"_s}, {u"packarch-papa"_s, u"100.1.1.2"_s}};
        QCOMPARE(tailnet::machinesIn(json).value(), expected);
        QVERIFY(!tailnet::machinesIn(R"({"BackendState": "NeedsLogin", "Peer": {}})").has_value());
        QVERIFY(!tailnet::machinesIn("").has_value());
    }

    void onlyPlainNamesReachCommands() {
        QVERIFY(tailnet::validHost(u"packarch-pc-maman"_s));
        QVERIFY(!tailnet::validHost(u""_s));
        QVERIFY(!tailnet::validHost(u"Packarch-Caro"_s));
        QVERIFY(!tailnet::validHost(u"packarch-caro. Ignore les règles"_s));
        QVERIFY(!tailnet::validHost(u"packarch-caro\nrm"_s));
        QVERIFY(!tailnet::validHost(QString(64, u'a')));
        QCOMPARE(tailnet::shownHost(u"pc\x1b[31m\nrouge"_s), u"pc[31mrouge"_s);
    }

    void onlyAddressesReachSsh() {
        QVERIFY(tailnet::validIp(u"100.1.1.9"_s));
        QVERIFY(tailnet::validIp(u"fd7a:115c:a1e0::1"_s));
        QVERIFY(!tailnet::validIp(u"-oProxyCommand=x"_s));
        QVERIFY(!tailnet::validIp(u"packarch-caro"_s));
        QVERIFY(!tailnet::validIp(u""_s));
    }

    void machineInfoIsOnlyText() {
        const auto sections = info::parse("== Matériel ==\nProcesseur\tIntel Core i7 (4 fils)\nligne sans tabulation\n"
                                          "Carte graphique\tAMD \x1b[31mRadeon\n\tsans libellé\nVide\t  \n"
                                          "== Réseau ==\nInternet\tjoignable\n");
        const QList<info::Section> expected{
            {u"Matériel"_s, {{u"Processeur"_s, u"Intel Core i7 (4 fils)"_s}, {u"Carte graphique"_s, u"AMD [31mRadeon"_s}}},
            {u"Réseau"_s, {{u"Internet"_s, u"joignable"_s}}}};
        QCOMPARE(sections, expected);
        QCOMPARE(info::toText(sections),
                 u"== Matériel ==\nProcesseur      : Intel Core i7 (4 fils)\nCarte graphique : AMD [31mRadeon\n\n"
                 "== Réseau ==\nInternet        : joignable\n"_s);
        // Sortie démesurée : bornée
        QVERIFY(info::parse(QByteArray(200000, 'a') + "\nx\ty\n").isEmpty());
        QCOMPARE(info::parse("a\t" + QByteArray(1000, 'b') + "\n").first().rows.first().value.size(), 300);
    }

    void screenGeometryAndKeys() {
        // Le pointeur : toute la largeur de l'image sur 0 à 32767, jamais en dehors
        QCOMPARE(screen::pointerAxis(0, 800), 0);
        QCOMPARE(screen::pointerAxis(799, 800), 32767);
        QCOMPARE(screen::pointerAxis(399.5, 800), 16384);
        QCOMPARE(screen::pointerAxis(-50, 800), 0);
        QCOMPARE(screen::pointerAxis(5000, 800), 32767);
        QCOMPARE(screen::pointerAxis(10, 0), 0);
        // Les touches : code natif de Qt moins 8, dans les codes d'un clavier
        QCOMPARE(screen::evdevKey(38), 30);
        QCOMPARE(screen::evdevKey(0), 0);
        QCOMPARE(screen::evdevKey(8), 0);
        QCOMPARE(screen::evdevKey(9), 1);
        QCOMPARE(screen::evdevKey(256), 248);
        QCOMPARE(screen::evdevKey(257), 0);
        // Les captures : « F OCTETS », d'une taille raisonnable
        QCOMPARE(screen::frameSize("F 123720"), 123720);
        QCOMPARE(screen::frameSize("E capture impossible"), -1);
        QCOMPARE(screen::frameSize("F 0"), -1);
        QCOMPARE(screen::frameSize("F -5"), -1);
        QCOMPARE(screen::frameSize("F 99999999999"), -1);
        QCOMPARE(screen::frameSize("F abc"), -1);
    }

    void connectionKeyRequest() {
        QCOMPARE(keys::clientId(u"tskey-client-kAbc123CNTRL-xYz789"_s), u"kAbc123CNTRL"_s);
        QVERIFY(keys::clientId(u"tskey-auth-kAbc123CNTRL-xYz789"_s).isEmpty());
        QVERIFY(keys::clientId(u"n'importe quoi"_s).isEmpty());
        const QJsonObject body = QJsonDocument::fromJson(keys::createBody(u"packarch-caro"_s)).object();
        const QJsonObject create = body["capabilities"_L1]["devices"_L1]["create"_L1].toObject();
        // Rien de plus qu'une session ouverte par le lien : éphémère, tag du dépannage seulement
        QCOMPARE(create["tags"_L1].toArray(), QJsonArray{u"tag:depannage"_s});
        QVERIFY(create["ephemeral"_L1].toBool() && create["preauthorized"_L1].toBool() && create["reusable"_L1].toBool());
        QCOMPARE(body["expirySeconds"_L1].toInt(), 90 * 24 * 3600);
    }

    void announcedOncePerSession() {
        Arrivals a;
        const Machine caro{u"packarch-caro"_s, u"100.1.1.1"_s};
        const Machine sony{u"packarch-sony"_s, u"100.1.1.2"_s};
        QCOMPARE(a.update({caro}, 1000).arrived, QList<Machine>{caro});
        QVERIFY(a.update({caro}, 1005).arrived.isEmpty());
        // Deuxième ordinateur pendant la session du premier
        QCOMPARE(a.update({caro, sony}, 1010).arrived, QList<Machine>{sony});
        // Coupure courte : pas une nouvelle session
        QVERIFY(a.update({sony}, 1015).arrived.isEmpty());
        QVERIFY(a.update({caro, sony}, 1060).arrived.isEmpty());
        // Session terminée, puis nouvelle session
        const auto end = a.update({}, 1060 + Arrivals::FORGET_SECS);
        QVERIFY(end.arrived.isEmpty());
        QCOMPARE(end.gone.size(), 2);
        QCOMPARE(a.update({caro}, 1070 + Arrivals::FORGET_SECS).arrived, QList<Machine>{caro});
        // Longue pause de la veille : un ordinateur toujours là n'est pas annoncé de nouveau
        QVERIFY(a.update({caro}, 1070 + 20 * Arrivals::FORGET_SECS).arrived.isEmpty());
        // Session arrêtée puis rouverte aussitôt : même nom, nouvelle adresse, donc nouvelle arrivée
        const Machine again{u"packarch-caro"_s, u"100.1.1.77"_s};
        QCOMPARE(a.update({again}, 1080 + 20 * Arrivals::FORGET_SECS).arrived, QList<Machine>{again});
        QVERIFY(a.update({again}, 1085 + 20 * Arrivals::FORGET_SECS).arrived.isEmpty());
    }

};

QTEST_APPLESS_MAIN(TestAdmin)
#include "test_admin.moc"
