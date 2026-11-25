#include "screenview.hpp"

#include "chat.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {
/// Délai avant de relancer une capture perdue
constexpr int RETRY_MS = 3000;
/// Au plus un déplacement du pointeur toutes les 15 ms
constexpr int MOVE_MS = 15;

QByteArray resource(const QString& name) {
    QFile f(u":/"_s + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
}  // namespace

/// L'image, à la taille de l'onglet, et les gestes qui y sont faits
class ScreenCanvas : public QWidget {
public:
    explicit ScreenCanvas(ScreenView* view) : QWidget(view), view_(view) {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setMinimumSize(320, 200);
        moveTimer_.start();
    }

    void setImage(const QImage& image) {
        image_ = image;
        update();
    }
    const QImage& image() const { return image_; }
    void setControl(bool on) {
        control_ = on;
        setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
        if (!on)
            releaseAll();
    }

    /// Touches et boutons encore enfoncés : relâchés là-bas
    void releaseAll() {
        for (int key : std::as_const(keys_))
            view_->input("k " + QByteArray::number(key) + " 0\n");
        keys_.clear();
        for (int button : std::as_const(buttons_))
            view_->input("b " + QByteArray::number(button) + " 0\n");
        buttons_.clear();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (image_.isNull())
            return;
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target(), image_);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (!control_ || moveTimer_.elapsed() < MOVE_MS)
            return;
        moveTimer_.restart();
        sendPosition(e->position());
    }
    void mousePressEvent(QMouseEvent* e) override {
        setFocus();
        button(e, true);
    }
    void mouseReleaseEvent(QMouseEvent* e) override { button(e, false); }
    void mouseDoubleClickEvent(QMouseEvent* e) override { button(e, true); }
    void wheelEvent(QWheelEvent* e) override {
        if (!control_)
            return;
        // 120 par cran de molette
        wheel_ += e->angleDelta();
        const int v = wheel_.y() / 120;
        const int h = wheel_.x() / 120;
        if (v == 0 && h == 0)
            return;
        wheel_ -= QPoint(h * 120, v * 120);
        view_->input("w " + QByteArray::number(v) + " " + QByteArray::number(-h) + "\n");
    }

    void keyPressEvent(QKeyEvent* e) override { key(e, true); }
    void keyReleaseEvent(QKeyEvent* e) override { key(e, false); }
    /// Tab reste une touche (pas un changement de champ), et les raccourcis de la
    /// fenêtre ne prennent pas ce qui est tapé pour l'autre ordinateur
    bool event(QEvent* e) override {
        if (control_ && e->type() == QEvent::ShortcutOverride) {
            e->accept();
            return true;
        }
        if (control_ && e->type() == QEvent::KeyPress) {
            keyPressEvent(static_cast<QKeyEvent*>(e));
            return true;
        }
        return QWidget::event(e);
    }
    void focusOutEvent(QFocusEvent* e) override {
        releaseAll();
        QWidget::focusOutEvent(e);
    }

private:
    /// Rectangle de l'image dans l'onglet (proportions gardées, centrée)
    QRectF target() const {
        if (image_.isNull())
            return {};
        const QSizeF size = QSizeF(image_.size()).scaled(QSizeF(this->size()), Qt::KeepAspectRatio);
        return {(width() - size.width()) / 2, (height() - size.height()) / 2, size.width(), size.height()};
    }

    void sendPosition(const QPointF& pos) {
        const QRectF t = target();
        if (t.isEmpty())
            return;
        view_->input("m " + QByteArray::number(screen::pointerAxis(pos.x() - t.left(), t.width())) + " "
                     + QByteArray::number(screen::pointerAxis(pos.y() - t.top(), t.height())) + "\n");
    }

    void button(QMouseEvent* e, bool down) {
        if (!control_)
            return;
        const int n = e->button() == Qt::LeftButton ? 1 : e->button() == Qt::MiddleButton ? 2
                    : e->button() == Qt::RightButton ? 3 : 0;
        if (n == 0)
            return;
        // Le clic se fait là où il est donné, même si le pointeur n'a pas été suivi jusque-là
        sendPosition(e->position());
        if (down)
            buttons_.insert(n);
        else
            buttons_.remove(n);
        view_->input("b " + QByteArray::number(n) + (down ? " 1\n" : " 0\n"));
    }

    void key(QKeyEvent* e, bool down) {
        // La répétition est faite là-bas, par la touche restée enfoncée
        const int code = screen::evdevKey(e->nativeScanCode());
        if (!control_ || e->isAutoRepeat() || code == 0)
            return;
        if (down)
            keys_.insert(code);
        else
            keys_.remove(code);
        view_->input("k " + QByteArray::number(code) + (down ? " 1\n" : " 0\n"));
    }

    ScreenView* view_;
    QImage image_;
    bool control_ = false;
    QSet<int> keys_;
    QSet<int> buttons_;
    QPoint wheel_;
    QElapsedTimer moveTimer_;
};

ScreenView::ScreenView(Machine machine, QWidget* parent) : QWidget(parent), machine_(std::move(machine)) {
    canvas_ = new ScreenCanvas(this);
    control_ = new QCheckBox(u"Contrôler (souris et clavier)"_s);
    quality_ = new QComboBox;
    quality_->addItem(u"Nette"_s, QByteArray("75 1"));
    quality_->addItem(u"Légère (connexion lente)"_s, QByteArray("45 0.5"));
    pause_ = new QPushButton(u"Pause"_s);
    pause_->setCheckable(true);
    auto* saveButton = new QPushButton(u"Enregistrer l'image"_s);
    status_ = new QLabel;
    status_->setEnabled(false);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->addWidget(control_);
    bar->addSpacing(12);
    bar->addWidget(quality_);
    bar->addWidget(pause_);
    bar->addWidget(saveButton);
    bar->addStretch();
    bar->addWidget(status_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(canvas_, 1);

    connect(control_, &QCheckBox::toggled, this, &ScreenView::setControl);
    connect(pause_, &QPushButton::toggled, this, [this](bool paused) {
        if (!paused)
            requestFrame();
    });
    connect(saveButton, &QPushButton::clicked, this, &ScreenView::save);
    say(u"Connexion à l'écran…"_s);
    rate_.start();
}

ScreenView::~ScreenView() {
    for (QProcess* p : {capture_, input_}) {
        if (!p)
            continue;
        p->disconnect(this);
        p->closeWriteChannel();
        if (!p->waitForFinished(500))
            p->kill();
        p->waitForFinished(500);
    }
}

void ScreenView::say(const QString& text) {
    status_->setText(text);
}

void ScreenView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!capture_)
        startCapture();
    else
        requestFrame();
}

void ScreenView::hideEvent(QHideEvent* event) {
    // Plus rien n'est demandé tant que l'onglet n'est pas affiché
    canvas_->releaseAll();
    QWidget::hideEvent(event);
}

void ScreenView::startCapture() {
    if (capture_ || !tailnet::validIp(machine_.ip))
        return;
    const QByteArray script = resource(u"capture.sh"_s);
    if (script.isEmpty())
        return;
    buffer_.clear();
    expected_ = -1;
    waiting_ = false;
    capture_ = new QProcess(this);
    // Le script est dans la commande (en base64 : rien pour le shell distant),
    // l'entrée standard sert aux demandes de capture
    QStringList args = chat::sshOptions(true);
    args << u"root@%1"_s.arg(machine_.ip)
         << u"packarch-depannage session-run 'eval \"$(echo %1 | base64 -d)\"'"_s.arg(QString::fromLatin1(script.toBase64()));
    connect(capture_, &QProcess::started, this, &ScreenView::requestFrame);
    connect(capture_, &QProcess::readyReadStandardOutput, this, &ScreenView::readCapture);
    const auto ended = [this] {
        if (!capture_)
            return;
        capture_->deleteLater();
        capture_ = nullptr;
        waiting_ = false;
        say(u"Écran injoignable, nouvel essai…"_s);
        QTimer::singleShot(RETRY_MS, this, [this] {
            if (isVisible())
                startCapture();
        });
    };
    connect(capture_, &QProcess::finished, this, ended);
    connect(capture_, &QProcess::errorOccurred, this, [ended](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            ended();
    });
    capture_->setStandardErrorFile(QProcess::nullDevice());
    capture_->start(u"ssh"_s, args);
}

void ScreenView::requestFrame() {
    if (!capture_ || capture_->state() != QProcess::Running || waiting_ || pause_->isChecked() || !isVisible())
        return;
    waiting_ = true;
    capture_->write(quality_->currentData().toByteArray() + "\n");
}

void ScreenView::readCapture() {
    buffer_ += capture_->readAllStandardOutput();
    while (true) {
        if (expected_ < 0) {
            const qsizetype end = buffer_.indexOf('\n');
            if (end < 0) {
                // Pas un en-tête : la suite ne serait pas lisible non plus
                if (buffer_.size() > 200)
                    capture_->kill();
                return;
            }
            const QByteArray header = buffer_.left(end);
            buffer_.remove(0, end + 1);
            expected_ = screen::frameSize(header);
            if (expected_ < 0) {
                waiting_ = false;
                if (header.startsWith("E "))
                    say(QString::fromUtf8(header.mid(2)).left(80));
                else {
                    capture_->kill();
                    return;
                }
                // Capture impossible pour l'instant (écran verrouillé, session fermée) : redemandée plus tard
                QTimer::singleShot(RETRY_MS, this, &ScreenView::requestFrame);
                continue;
            }
        }
        if (buffer_.size() < expected_)
            return;
        const QImage image = QImage::fromData(buffer_.left(expected_), "JPG");
        buffer_.remove(0, expected_);
        expected_ = -1;
        waiting_ = false;
        if (!image.isNull()) {
            canvas_->setImage(image);
            ++frames_;
            if (rate_.elapsed() >= 2000) {
                say(u"%1 × %2 · %3 images/s"_s.arg(image.width()).arg(image.height()).arg(frames_ * 1000.0 / rate_.elapsed(), 0, 'f', 1));
                frames_ = 0;
                rate_.restart();
            }
        }
        // La suivante part quand celle-ci est arrivée : le rythme suit le débit
        QTimer::singleShot(30, this, &ScreenView::requestFrame);
    }
}

void ScreenView::setControl(bool on) {
    canvas_->setControl(on);
    if (!on) {
        if (input_) {
            input_->disconnect(this);
            input_->closeWriteChannel();
            connect(input_, &QProcess::finished, input_, &QObject::deleteLater);
            input_ = nullptr;
        }
        inputReady_ = false;
        return;
    }
    if (input_ || !tailnet::validIp(machine_.ip))
        return;
    const QByteArray script = resource(u"saisie.py"_s);
    if (script.isEmpty())
        return;
    input_ = new QProcess(this);
    QStringList args = chat::sshOptions(true);
    args << u"root@%1"_s.arg(machine_.ip)
         << u"python3 -u -c \"import base64;exec(base64.b64decode('%1'))\""_s.arg(QString::fromLatin1(script.toBase64()));
    connect(input_, &QProcess::readyReadStandardOutput, this, [this] {
        // « ok » : les périphériques virtuels sont créés
        if (input_->readAllStandardOutput().contains("ok")) {
            inputReady_ = true;
            canvas_->setFocus();
        }
    });
    const auto ended = [this] {
        if (!input_)
            return;
        input_->deleteLater();
        input_ = nullptr;
        inputReady_ = false;
        const QSignalBlocker blocker(control_);
        control_->setChecked(false);
        canvas_->setControl(false);
        say(u"Contrôle impossible sur cet ordinateur (python3 ou /dev/uinput absent ?)."_s);
    };
    connect(input_, &QProcess::finished, this, ended);
    connect(input_, &QProcess::errorOccurred, this, [ended](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            ended();
    });
    input_->setStandardErrorFile(QProcess::nullDevice());
    input_->start(u"ssh"_s, args);
}

void ScreenView::input(const QByteArray& line) {
    if (input_ && inputReady_ && input_->state() == QProcess::Running)
        input_->write(line);
}

void ScreenView::save() {
    const QImage image = canvas_->image();
    if (image.isNull())
        return;
    QString name = u"%1/depannage-%2-%3.png"_s.arg(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                    tailnet::validHost(machine_.host) ? machine_.host : u"ordinateur"_s,
                                                    QDateTime::currentDateTime().toString(u"yyyy-MM-dd_HH-mm-ss"_s));
    name = QFileDialog::getSaveFileName(this, u"Enregistrer l'image"_s, name, u"Images PNG (*.png)"_s);
    if (!name.isEmpty() && !image.save(name, "PNG"))
        say(u"Image non enregistrée."_s);
}
