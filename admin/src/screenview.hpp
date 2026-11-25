// Écran(s) de l'ordinateur dépanné, dans un onglet : des captures en continu,
// et la souris et le clavier relayés quand « Contrôler » est coché.
//
// Rien n'est installé là-bas : la capture (data/capture.sh, dans le bureau de
// l'utilisateur) et la saisie (data/saisie.py, périphériques virtuels du noyau)
// sont des scripts de Packarch Admin envoyés par Tailscale SSH. Les captures
// s'arrêtent quand l'onglet n'est pas affiché.
#pragma once

#include "screenproto.hpp"
#include "tailnet.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QSet>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QProcess;
class QPushButton;
class ScreenCanvas;

class ScreenView : public QWidget {
    Q_OBJECT
public:
    explicit ScreenView(Machine machine, QWidget* parent = nullptr);
    ~ScreenView() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    friend class ScreenCanvas;
    void startCapture();
    void readCapture();
    void requestFrame();
    void setControl(bool on);
    /// Ligne pour data/saisie.py (rien si le contrôle n'est pas prêt)
    void input(const QByteArray& line);
    void say(const QString& text);
    void save();

    Machine machine_;
    QProcess* capture_ = nullptr;
    QProcess* input_ = nullptr;
    bool inputReady_ = false;
    QByteArray buffer_;
    /// Taille de la capture en cours de réception (-1 : en attente d'un en-tête)
    qint64 expected_ = -1;
    /// Une capture est demandée et pas encore arrivée
    bool waiting_ = false;
    QElapsedTimer rate_;
    int frames_ = 0;

    ScreenCanvas* canvas_;
    QCheckBox* control_;
    QComboBox* quality_;
    QPushButton* pause_;
    QLabel* status_;
};
