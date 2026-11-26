#include "aur.hpp"
#include "chatfile.hpp"
#include "chatview.hpp"
#include "session.hpp"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

class TestDepannage : public QObject {
    Q_OBJECT
private slots:
    void statusJsonRoundTrip() {
        session::Status s;
        s.active = true;
        s.end = 10;
        s.hostname = u"packarch-pc"_s;
        s.helperName = u"Fabrice"_s;
        const QJsonObject json = s.toJson();
        QCOMPARE(session::Status::fromJson(json), s);
        // Les noms lus par les anciennes versions et par la compétence Claude
        for (const char* key : {"active", "end", "hostname", "backend", "auth_url", "dns_name", "ssh_sessions",
                                "helper", "last_activity", "helper_name", "helper_login"})
            QVERIFY2(json.contains(QLatin1StringView(key)), key);
        QVERIFY(json.value("auth_url"_L1).isNull());
    }

    void peers() {
        const QJsonObject j = QJsonDocument::fromJson(R"({"Peer": {
            "a": {"HostName": "unraid", "RxBytes": 0, "LastWrite": "0001-01-01T00:00:00Z", "UserID": 7},
            "b": {"HostName": "archlinux", "RxBytes": 600, "LastWrite": "2026-11-01T12:06:30.469348563+01:00", "UserID": 7},
            "c": {"HostName": "minipc", "RxBytes": 10, "LastWrite": "2026-11-01T10:00:00Z", "UserID": 8}
        }, "User": {"7": {"LoginName": "fab@example.com", "DisplayName": "Fabrice"}}})").object();
        const session::Peer expected{u"archlinux"_s, 1793531190, u"Fabrice"_s, u"fab@example.com"_s};
        QCOMPARE(session::lastPeer(j).value(), expected);
        QVERIFY(!session::unixTime(u"0001-01-01T00:00:00Z"_s).has_value());
        QCOMPARE(session::unixTime(u"2026-11-01T10:00:00Z"_s).value(), 1793527200);
    }

    void machineNames() {
        QCOMPARE(session::machineName(u"PC-Maman\n"_s), u"packarch-pc-maman"_s);
        QCOMPARE(session::machineName(u"Salon de Mamie"_s), u"packarch-salon-de-mamie"_s);
        QCOMPARE(session::machineName(u"packarch"_s), u"packarch"_s);
        QCOMPARE(session::machineName(u"  \n"_s), u"packarch-ordinateur"_s);
        QCOMPARE(session::machineName(u"éé"_s), u"packarch-ordinateur"_s);
    }

    void helperNamesAndEmoji() {
        QVERIFY(session::validHelperName(u"antidote"_s));
        QVERIFY(session::validHelperName(u"Fabrice C."_s));
        QVERIFY(session::validHelperName(u"Éloïse"_s));
        QVERIFY(!session::validHelperName(u""_s));
        QVERIFY(!session::validHelperName(u" antidote"_s));
        QVERIFY(!session::validHelperName(u"root\n<b>"_s));
        QVERIFY(!session::validHelperName(u"support (officiel)"_s));
        QVERIFY(!session::validHelperName(QString(33, u'a')));
        session::Status st;
        st.active = true;
        st.sshSessions = 1;
        st.helperName = u"antidote"_s;
        st.helperLogin = u"quelquun@example.com"_s;
        st.helper = u"packarch"_s;
        const QString text = session::describe(st).text;
        QVERIFY2(text.contains(u"cet ordinateur : antidote. "_s), qPrintable(text));
        QVERIFY(!text.contains(u"example.com"_s) && !text.contains(u"packarch"_s));
        // Émoticônes tapées : entre des blancs seulement
        QCOMPARE(chat::withEmoji(u"Merci :) ça marche <3"_s), u"Merci 🙂 ça marche ❤️"_s);
        QCOMPARE(chat::withEmoji(u"voir http://x:8080/a:)b et C:D"_s), u"voir http://x:8080/a:)b et C:D"_s);
        QCOMPARE(chat::withEmoji(u"ligne 1 ;)\n:("_s), u"ligne 1 😉\n😕"_s);
        QCOMPARE(chat::clean(u"ok 👍 ❤️"_s), u"ok 👍 ❤️"_s);
    }

    void chatLineKeepsTextAsText() {
        ChatMessage m{false, 1, u"x"_s, u"Claude"_s};
        QCOMPARE(ChatView::roleOf(m), ChatView::Role::Assistant);
        m.name = u"antidote"_s;
        QCOMPARE(ChatView::roleOf(m), ChatView::Role::Helper);
        m.fromUser = true;
        m.name = u"Claude"_s;  // la personne ne passe pas pour l'assistant en prenant son nom
        QCOMPARE(ChatView::roleOf(m), ChatView::Role::Person);
        const QString line = ChatView::html(u"caro"_s, u"<b>gras</b> & <a href='x'>lien</a>\nsuite"_s, u"23:55"_s, ChatView::Role::Person);
        QVERIFY(line.contains(u"&lt;b&gt;gras&lt;/b&gt; &amp; &lt;a href="_s));
        QVERIFY(line.contains(u"<br>suite"_s));
        QVERIFY(line.contains(u">caro:</span>"_s) && line.contains(u"23:55"_s));
        QVERIFY(!line.contains(u"<b>"_s) && !line.contains(u"<a "_s));
    }

    void connectionKeys() {
        QVERIFY(session::validKey(u"tskey-auth-kAbc123CNTRL-xYz789"_s));
        QVERIFY(!session::validKey(u""_s));
        QVERIFY(!session::validKey(u"tskey-client-kAbc123CNTRL-xYz789"_s));
        QVERIFY(!session::validKey(u"tskey-auth-kAbc CNTRL-x"_s));
        QVERIFY(!session::validKey(u"tskey-auth-a-b\n--timeout=0"_s));
        QVERIFY(!session::validKey(u"file:/etc/shadow"_s));
        QVERIFY(!session::validKey(u"tskey-auth-a-"_s + QString(300, u'b')));
    }

    void cleanedText() {
        QCOMPARE(chat::clean(u"  bonjour\x1b[31m\ttoi\u009b\n "_s), u"bonjour[31m toi"_s);
        QCOMPARE(chat::clean(u"a\nb"_s), u"a\nb"_s);
        QCOMPARE(chat::clean(QString(chat::MAX_CHARS + 5, u'é')).size(), chat::MAX_CHARS);
    }

    void invalidLinesIgnored() {
        QVERIFY(!chat::parseLine(""));
        QVERIFY(!chat::parseLine("{pas du json"));
        QVERIFY(!chat::parseLine(R"({"from":"root","time":1,"text":"x"})"));
        QVERIFY(!chat::parseLine(R"({"from":"user","time":1,"text":" \u001b "})"));
        const ChatMessage expected{false, 1, u"ok"_s, {}};
        QCOMPARE(chat::parseLine(R"({"from":"helper","time":1,"text":"ok"})").value(), expected);
        // Le format écrit est celui qui est lu
        QCOMPARE(chat::parseLine(chat::toLine({true, 5, u"ça va ?"_s, {}}).trimmed()).value(), (ChatMessage{true, 5, u"ça va ?"_s, {}}));
        // Le nom affiché devant le message : gardé s'il est présentable, sinon ignoré
        QCOMPARE(chat::parseLine(chat::toLine({false, 6, u"je regarde"_s, u"Claude"_s}).trimmed()).value().name, u"Claude"_s);
        QVERIFY(chat::parseLine(R"({"from":"helper","time":1,"text":"ok","name":"<b>root</b>"})").value().name.isEmpty());
        QVERIFY(chat::parseLine(R"({"from":"user","time":1,"text":"ok","name":7})").value().name.isEmpty());
        QCOMPARE(chat::shown(u"antidote"_s, u"bonjour"_s), u"antidote: bonjour"_s);
    }

    void notificationFitsTwoLines() {
        QCOMPARE(chat::notificationBody(u"  a\nb  c "_s), u"a b c"_s);
        QCOMPARE(chat::notificationBody(QString(36, u'é')), QString(36, u'é'));
        QCOMPARE(chat::notificationBody(QString(37, u'é')), QString(36, u'é') + u'…');
        QCOMPARE(chat::notificationBody(u"a <b> & c"_s), u"a &lt;b&gt; &amp; c"_s);
    }

    void aurSudoRule() {
        // pacman seulement, pour ce compte seulement
        const QString rule = aur::sudoersRule(u"maman"_s);
        QVERIFY(rule.endsWith(u"maman ALL=(root) NOPASSWD: /usr/bin/pacman\n"_s));
        // Rien pour root, ni pour un nom qui écrirait autre chose dans la règle
        QVERIFY(aur::sudoersRule(u"root"_s).isEmpty());
        QVERIFY(aur::sudoersRule(u"ALL"_s).isEmpty());
        QVERIFY(aur::sudoersRule(u"a b"_s).isEmpty());
        QVERIFY(aur::sudoersRule(u"x\ny ALL=(ALL) NOPASSWD: ALL"_s).isEmpty());
        QVERIFY(aur::sudoersRule(QString()).isEmpty());
        // sudo ignore les fichiers dont le nom contient un point
        QVERIFY(!aur::sudoersPath().section(u'/', -1).contains(u'.'));
    }

    void appendedThenReadOnce() {
        QTemporaryDir dir;
        const QString p = dir.filePath(u"chat.jsonl"_s);
        const auto write = [&p](const QByteArray& data, QIODevice::OpenMode mode) {
            QFile f(p);
            QVERIFY(f.open(mode));
            f.write(data);
        };
        write("", QIODevice::WriteOnly);
        qint64 pos = 0;
        QVERIFY(chatfile::appendTo(p, true, u"bonjour\nça va ?"_s).isEmpty());
        QVERIFY(chatfile::appendTo(p, true, u"   "_s).isEmpty());
        const auto got = chatfile::readNew(p, pos);
        QCOMPARE(got.size(), 1);
        QCOMPARE(got[0].text, u"bonjour\nça va ?"_s);
        QVERIFY(got[0].fromUser);
        QVERIFY(chatfile::readNew(p, pos).isEmpty());
        // Ligne en cours d'écriture : attendue jusqu'à sa fin
        write(R"({"from":"helper",)", QIODevice::Append);
        QVERIFY(chatfile::readNew(p, pos).isEmpty());
        write("\"time\":1,\"text\":\"oui\"}\n", QIODevice::Append);
        QCOMPARE(chatfile::readNew(p, pos).value(0).text, u"oui"_s);
        // Fichier recréé (nouvelle session) : relu depuis le début
        write("", QIODevice::WriteOnly);
        QVERIFY(chatfile::appendTo(p, true, u"x"_s).isEmpty());
        QCOMPARE(chatfile::readNew(p, pos).value(0).text, u"x"_s);
        // Message signé : le nom est écrit avec lui ; un nom inattendu n'est pas écrit
        QVERIFY(chatfile::appendTo(p, false, u"je m'en occupe"_s, u"Claude"_s).isEmpty());
        QVERIFY(chatfile::appendTo(p, true, u"merci"_s, u"caro\n{}"_s).isEmpty());
        const auto signedMessages = chatfile::readNew(p, pos);
        QCOMPARE(signedMessages.value(0).name, u"Claude"_s);
        QVERIFY(signedMessages.value(1).name.isEmpty());
        // Sans fichier (pas de session) : une erreur, rien de créé
        QVERIFY(!chatfile::appendTo(dir.filePath(u"absent"_s), true, u"x"_s).isEmpty());
        QVERIFY(!QFile::exists(dir.filePath(u"absent"_s)));
    }
};

QTEST_APPLESS_MAIN(TestDepannage)
#include "test_depannage.moc"
