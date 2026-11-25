# Souris et clavier d'un ordinateur dépanné, pour Packarch Admin : lancé en root
# par Tailscale SSH (python3 de la bibliothèque standard seulement, rien n'est
# installé). Crée deux périphériques d'entrée virtuels du noyau (uinput), que
# tous les bureaux prennent comme un vrai clavier et une vraie tablette, et y
# rejoue les lignes lues sur l'entrée standard :
#   m X Y    pointeur en X, Y (0 à 32767 : toute la largeur, toute la hauteur)
#   b N E    bouton N (1 gauche, 2 milieu, 3 droit), E : 1 enfoncé, 0 relâché
#   w V H    molette, crans verticaux et horizontaux
#   k C E    touche de code noyau C (evdev), E : 1 enfoncée, 0 relâchée
# Les périphériques disparaissent à la fin (entrée fermée, connexion perdue).
import fcntl, os, struct, sys, time

UI_SET_EVBIT, UI_SET_KEYBIT, UI_SET_RELBIT, UI_SET_ABSBIT = 0x40045564, 0x40045565, 0x40045566, 0x40045567
UI_DEV_CREATE, UI_DEV_DESTROY = 0x5501, 0x5502
EV_SYN, EV_KEY, EV_REL, EV_ABS = 0, 1, 2, 3
ABS_X, ABS_Y, REL_HWHEEL, REL_WHEEL = 0, 1, 6, 8
BUTTONS = {1: 0x110, 2: 0x112, 3: 0x111}
MAX = 32767
KEYS = range(1, 249)


def device(name, product, keys, rel=(), absolute=()):
    fd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)
    fcntl.ioctl(fd, UI_SET_EVBIT, EV_KEY)
    for k in keys:
        fcntl.ioctl(fd, UI_SET_KEYBIT, k)
    if rel:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_REL)
        for r in rel:
            fcntl.ioctl(fd, UI_SET_RELBIT, r)
    absmax = [0] * 64
    if absolute:
        fcntl.ioctl(fd, UI_SET_EVBIT, EV_ABS)
        for a in absolute:
            fcntl.ioctl(fd, UI_SET_ABSBIT, a)
            absmax[a] = MAX
    # struct uinput_user_dev : nom, identifiants (bus virtuel), puis les bornes des axes
    os.write(fd, struct.pack("80sHHHHI", name.encode(), 0x06, 0x1911, product, 1, 0)
             + struct.pack("64i", *absmax) + struct.pack("192i", *([0] * 192)))
    fcntl.ioctl(fd, UI_DEV_CREATE)
    return fd


def emit(fd, kind, code, value):
    now = time.time()
    os.write(fd, struct.pack("llHHi", int(now), int(now % 1 * 1e6), kind, code, value))


def clamp(v, low, high):
    return max(low, min(high, v))


pointer = device("Packarch Admin (pointeur)", 1, BUTTONS.values(), (REL_WHEEL, REL_HWHEEL), (ABS_X, ABS_Y))
keyboard = device("Packarch Admin (clavier)", 2, KEYS)
held = set()
# Le bureau met un instant à voir les nouveaux périphériques
time.sleep(1)
print("ok", flush=True)
try:
    for line in sys.stdin:
        part = line.split()
        try:
            cmd, a, b = part[0], int(part[1]), int(part[2])
        except (IndexError, ValueError):
            continue
        if cmd == "m":
            emit(pointer, EV_ABS, ABS_X, clamp(a, 0, MAX))
            emit(pointer, EV_ABS, ABS_Y, clamp(b, 0, MAX))
            emit(pointer, EV_SYN, 0, 0)
        elif cmd == "b" and a in BUTTONS:
            emit(pointer, EV_KEY, BUTTONS[a], 1 if b else 0)
            emit(pointer, EV_SYN, 0, 0)
        elif cmd == "w":
            if a:
                emit(pointer, EV_REL, REL_WHEEL, clamp(a, -10, 10))
            if b:
                emit(pointer, EV_REL, REL_HWHEEL, clamp(b, -10, 10))
            emit(pointer, EV_SYN, 0, 0)
        elif cmd == "k" and a in KEYS:
            (held.add if b else held.discard)(a)
            emit(keyboard, EV_KEY, a, 1 if b else 0)
            emit(keyboard, EV_SYN, 0, 0)
finally:
    # Aucune touche ni bouton ne reste enfoncé après la fin
    for k in held:
        emit(keyboard, EV_KEY, k, 0)
    emit(keyboard, EV_SYN, 0, 0)
    for button in BUTTONS.values():
        emit(pointer, EV_KEY, button, 0)
    emit(pointer, EV_SYN, 0, 0)
    for fd in (pointer, keyboard):
        fcntl.ioctl(fd, UI_DEV_DESTROY)
        os.close(fd)
