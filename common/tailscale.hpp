// Tailscale sur cet ordinateur.
#pragma once

#include <QJsonObject>
#include <optional>

namespace tailscale {

/// Tag Tailscale des machines dépannées (règles d'accès du réseau Tailscale)
inline constexpr char TAG[] = "tag:depannage";

/// Sortie JSON de « tailscale status » ; rien si tailscaled ne répond pas.
/// Attend la réponse : pas dans le fil d'une fenêtre.
std::optional<QJsonObject> status();

}  // namespace tailscale
