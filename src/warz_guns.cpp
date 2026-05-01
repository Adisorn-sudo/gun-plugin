#include <endstone/endstone.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::chrono;

namespace {

struct GunDef {
    std::string key;
    std::string display_name;
    std::string particle_name;
    int max_ammo;
    int damage;
    float range;
    float step;
    milliseconds fire_cooldown;
};

struct GunState {
    int ammo = 0;
    steady_clock::time_point last_shot{};
};

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::vector<std::string> splitLore(const std::string &line1, const std::string &line2)
{
    return {line1, line2};
}

} // namespace

class WarzGunsPlugin : public endstone::Plugin {
public:
    void onEnable() override
    {
        registerEvent(&WarzGunsPlugin::onPlayerInteract, *this);
        getLogger().info("WarzGuns enabled");
    }

    bool onCommand(endstone::CommandSender &sender, const endstone::Command &command, const std::vector<std::string> &args) override
    {
        if (command.getName() != "gun") {
            return false;
        }

        auto *player = sender.asPlayer();
        if (!player) {
            sender.sendMessage("This command can only be used by a player.");
            return true;
        }

        if (args.empty()) {
            player->sendMessage("Usage: /gun <ak47|glock|sniper|reload|ammo>");
            return true;
        }

        const std::string action = toLower(args[0]);
        const GunDef *gun = nullptr;

        if (action == "ak47") {
            static const GunDef def{"ak47", "§cAK-47", "minecraft:basic_flame_particle", 30, 4, 28.0f, 0.45f, milliseconds(110)};
            gun = &def;
        } else if (action == "glock") {
            static const GunDef def{"glock", "§eGlock", "minecraft:basic_flame_particle", 12, 3, 20.0f, 0.40f, milliseconds(180)};
            gun = &def;
        } else if (action == "sniper") {
            static const GunDef def{"sniper", "§bSniper", "minecraft:basic_flame_particle", 5, 10, 90.0f, 0.70f, milliseconds(900)};
            gun = &def;
        }

        if (action == "reload") {
            const auto name = player->getName();
            auto it = gun_states_.find(name);
            if (it == gun_states_.end() || it->second.ammo <= 0) {
                player->sendMessage("No gun ammo to reload.");
                return true;
            }

            const std::string held = getHeldGunKey(*player);
            if (held.empty()) {
                player->sendMessage("Hold a gun first.");
                return true;
            }

            auto g = guns_.find(held);
            if (g == guns_.end()) {
                player->sendMessage("That item is not a registered gun.");
                return true;
            }

            it->second.ammo = g->second.max_ammo;
            player->sendMessage("Reloaded.");
            return true;
        }

        if (action == "ammo") {
            const std::string held = getHeldGunKey(*player);
            if (held.empty()) {
                player->sendMessage("Hold a gun first.");
                return true;
            }

            auto g = guns_.find(held);
            if (g == guns_.end()) {
                player->sendMessage("That item is not a registered gun.");
                return true;
            }

            auto &state = gun_states_[player->getName()];
            if (state.ammo <= 0) {
                state.ammo = g->second.max_ammo;
            }
            player->sendMessage("Ammo: " + std::to_string(state.ammo) + "/" + std::to_string(g->second.max_ammo));
            return true;
        }

        if (!gun) {
            player->sendMessage("Unknown gun. Use /gun ak47, /gun glock, /gun sniper");
            return true;
        }

        giveGun(*player, *gun);
        player->sendMessage("Given: " + gun->display_name);
        return true;
    }

    void onPlayerInteract(endstone::PlayerInteractEvent &event)
    {
        using Action = endstone::PlayerInteractEvent::Action;

        if (event.getAction() != Action::RightClickAir && event.getAction() != Action::RightClickBlock) {
            return;
        }

        auto &player = event.getPlayer();
        auto *item = event.getItem();
        if (!item) {
            return;
        }

        const std::string key = getGunKey(*item);
        if (key.empty()) {
            return;
        }

        event.setCancelled(true);
        shootGun(player, key);
    }

private:
    std::unordered_map<std::string, GunDef> guns_ {
        {"ak47", {"ak47", "§cAK-47", "minecraft:basic_flame_particle", 30, 4, 28.0f, 0.45f, milliseconds(110)}},
        {"glock", {"glock", "§eGlock", "minecraft:basic_flame_particle", 12, 3, 20.0f, 0.40f, milliseconds(180)}},
        {"sniper", {"sniper", "§bSniper", "minecraft:basic_flame_particle", 5, 10, 90.0f, 0.70f, milliseconds(900)}}
    };

    std::unordered_map<std::string, GunState> gun_states_;

    static std::string getGunKey(const endstone::ItemStack &item)
    {
        try {
            auto meta = item.getItemMeta();
            const std::string display = meta.getDisplayName();
            if (display == "§cAK-47") return "ak47";
            if (display == "§eGlock") return "glock";
            if (display == "§bSniper") return "sniper";
        } catch (...) {
            // Item may have no meta or the API may throw on plain items.
        }
        return {};
    }

    std::string getHeldGunKey(endstone::Player &player)
    {
        auto &item = player.getInventory().getItemInMainHand();
        return getGunKey(item);
    }

    void giveGun(endstone::Player &player, const GunDef &gun)
    {
        // One stable base item is enough for the first WarZ prototype.
        // If your build uses a different enum spelling for ItemType, swap this one line.
        endstone::ItemStack item(endstone::ItemType::Stick, 1);

        auto meta = item.getItemMeta();
        meta.setDisplayName(gun.display_name);
        meta.setLore(splitLore(
            "§7Right click to shoot",
            "§7Reload with /gun reload"
        ));
        meta.setUnbreakable(true);
        item.setItemMeta(meta);

        player.getInventory().setItemInMainHand(item);
        gun_states_[player.getName()] = GunState{gun.max_ammo, steady_clock::now() - gun.fire_cooldown};
    }

    void shootGun(endstone::Player &player, const std::string &gun_key)
    {
        auto g = guns_.find(gun_key);
        if (g == guns_.end()) {
            return;
        }

        auto &state = gun_states_[player.getName()];
        const auto now = steady_clock::now();
        if (state.last_shot.time_since_epoch().count() != 0 && now - state.last_shot < g->second.fire_cooldown) {
            return;
        }

        if (state.ammo <= 0) {
            player.sendMessage("Out of ammo. Use /gun reload");
            return;
        }

        state.last_shot = now;
        state.ammo -= 1;

        auto start = player.getLocation();
        auto direction = start.getDirection();
        direction.normalize();

        // Muzzle flash at the shooter.
        player.spawnParticle(g->second.particle_name, start);

        const float range = g->second.range;
        const float step = g->second.step;

        bool hit = false;
        endstone::Player *hit_player = nullptr;

        for (float d = 0.0f; d <= range; d += step) {
            auto point = start;
            point += direction * d;

            player.spawnParticle(g->second.particle_name, point);

            const auto point_pos = static_cast<endstone::Vector>(point);

            for (auto &candidate : player.getServer().getOnlinePlayers()) {
                if (candidate.getName() == player.getName()) {
                    continue;
                }

                const auto candidate_pos = static_cast<endstone::Vector>(candidate.getLocation());
                const auto diff = candidate_pos - point_pos;
                if (diff.lengthSquared() <= 1.0f) {
                    hit = true;
                    hit_player = &candidate;
                    break;
                }
            }

            if (hit) {
                break;
            }
        }

        player.playSound(start, "random.explode", 1.0f, 1.5f);

        if (hit && hit_player) {
            const float new_health = std::max(0.0f, hit_player->getHealth() - static_cast<float>(g->second.damage));
            hit_player->setHealth(new_health);
            player.sendMessage("Hit " + hit_player->getName() + " for " + std::to_string(g->second.damage));
        }

        if (state.ammo <= 0) {
            player.sendMessage("Reload with /gun reload");
        }
    }
};

ENDSTONE_PLUGIN("warz_guns", "0.1.0", WarzGunsPlugin)
{
    description = "WarZ gun prototype with /gun, tracer particles, ammo, reload, and hitscan damage";

    command("gun")
        .description("Give and use WarZ guns.")
        .usages("/gun <ak47|glock|sniper|reload|ammo>")
        .permissions("warz.command.gun");

    permission("warz.command.gun")
        .description("Allow players to use /gun")
        .default_(endstone::PermissionDefault::True);
}
