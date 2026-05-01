#include <endstone/endstone.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

using namespace std::chrono;

namespace {

struct GunDef {
    std::string key;
    std::string display_name;
    std::string particle_name;
    int max_ammo;
    int damage;
    double range;
    double step;
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

        if (action == "reload") {
            const std::string held = getHeldGunKey(*player);
            if (held.empty()) {
                player->sendMessage("Hold a gun first.");
                return true;
            }

            const auto it = guns_.find(held);
            if (it == guns_.end()) {
                player->sendMessage("That item is not a registered gun.");
                return true;
            }

            auto &state = gun_states_[player->getName()];
            state.ammo = it->second.max_ammo;
            player->sendMessage("Reloaded.");
            return true;
        }

        if (action == "ammo") {
            const std::string held = getHeldGunKey(*player);
            if (held.empty()) {
                player->sendMessage("Hold a gun first.");
                return true;
            }

            const auto it = guns_.find(held);
            if (it == guns_.end()) {
                player->sendMessage("That item is not a registered gun.");
                return true;
            }

            auto &state = gun_states_[player->getName()];
            if (state.ammo <= 0) {
                state.ammo = it->second.max_ammo;
            }
            player->sendMessage("Ammo: " + std::to_string(state.ammo) + "/" + std::to_string(it->second.max_ammo));
            return true;
        }

        const GunDef *gun = nullptr;
        if (action == "ak47") {
            gun = &guns_.at("ak47");
        } else if (action == "glock") {
            gun = &guns_.at("glock");
        } else if (action == "sniper") {
            gun = &guns_.at("sniper");
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

        const auto &item = event.getItem(); // const std::optional<ItemStack>&
        if (!item) {
            return;
        }

        const std::string key = getGunKey(*item);
        if (key.empty()) {
            return;
        }

        event.setCancelled(true);
        shootGun(event.getPlayer(), key);
    }

private:
    std::unordered_map<std::string, GunDef> guns_ {
        {"ak47", {"ak47", "§cAK-47", "minecraft:basic_flame_particle", 30, 4, 28.0, 0.45, milliseconds(110)}},
        {"glock", {"glock", "§eGlock", "minecraft:basic_flame_particle", 12, 3, 20.0, 0.40, milliseconds(180)}},
        {"sniper", {"sniper", "§bSniper", "minecraft:basic_flame_particle", 5, 10, 90.0, 0.70, milliseconds(900)}}
    };

    std::unordered_map<std::string, GunState> gun_states_;

    static std::string getGunKey(const endstone::ItemStack &item)
    {
        if (!item.hasItemMeta()) {
            return {};
        }

        auto meta = item.getItemMeta();
        if (!meta || !meta->hasDisplayName()) {
            return {};
        }

        const std::string display = meta->getDisplayName();
        if (display == "§cAK-47") return "ak47";
        if (display == "§eGlock") return "glock";
        if (display == "§bSniper") return "sniper";
        return {};
    }

    std::string getHeldGunKey(endstone::Player &player)
    {
        auto item = player.getInventory().getItemInMainHand();
        if (!item) {
            return {};
        }
        return getGunKey(*item);
    }

    void giveGun(endstone::Player &player, const GunDef &gun)
    {
        endstone::ItemStack item("minecraft:stick", 1);

        auto meta = item.getItemMeta();
        if (!meta) {
            player.sendMessage("Failed to create item meta.");
            return;
        }

        meta->setDisplayName(std::optional<std::string>(gun.display_name));
        meta->setLore(std::optional<std::vector<std::string>>({
            "§7Right click to shoot",
            "§7Reload with /gun reload"
        }));
        meta->setUnbreakable(true);
        item.setItemMeta(meta.get());

        player.getInventory().setItemInMainHand(std::optional<endstone::ItemStack>(item));
        gun_states_[player.getName()] = GunState{gun.max_ammo, steady_clock::now() - gun.fire_cooldown};
    }

    void shootGun(endstone::Player &player, const std::string &gun_key)
    {
        const auto it = guns_.find(gun_key);
        if (it == guns_.end()) {
            return;
        }

        auto &state = gun_states_[player.getName()];
        const auto now = steady_clock::now();
        if (state.last_shot.time_since_epoch().count() != 0 && now - state.last_shot < it->second.fire_cooldown) {
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
        (void)direction.normalize();

        player.spawnParticle(it->second.particle_name, start);
        player.playSound(start, "random.explode", 1.0f, 1.5f);

        // ✅ Track hit targets
        std::set<std::string> hit_players;

        for (double d = 0.0; d <= it->second.range; d += it->second.step) {
            auto point = start;
            point += direction * d;

            player.spawnParticle(it->second.particle_name, point);

            const double px = point.getX();
            const double py = point.getY();
            const double pz = point.getZ();

            // ✅ Get all online players for this point
            std::vector<endstone::Player *> candidates = player.getServer().getOnlinePlayers();
            
            for (auto *candidate : candidates) {
                if (!candidate || candidate->getName() == player.getName()) {
                    continue;
                }

                // ✅ Skip if already hit
                if (hit_players.find(candidate->getName()) != hit_players.end()) {
                    continue;
                }

                const auto loc = candidate->getLocation();
                const double dx = loc.getX() - px;
                const double dy = loc.getY() - py;
                const double dz = loc.getZ() - pz;

                // ✅ Check collision with better radius (0.8)
                const double distance_sq = dx * dx + dy * dy + dz * dz;
                const double hit_radius = 0.8;

                if (distance_sq <= hit_radius * hit_radius) {
                    // ✅ HIT! Apply damage
                    int current_health = candidate->getHealth();
                    int new_health = std::max(0, current_health - it->second.damage);
                    candidate->setHealth(new_health);

                    // Log for debugging
                    player.sendMessage("§a[HIT] " + candidate->getName() + " §c-" + std::to_string(it->second.damage));
                    candidate->sendMessage("§c[SHOT] You were hit by " + player.getName());

                    // Mark as hit
                    hit_players.insert(candidate->getName());

                    // Hit effect
                    player.spawnParticle("minecraft:explosion_particle", loc);
                }
            }
        }

        // ✅ Feedback
        if (hit_players.empty()) {
            player.sendMessage("§7[MISS]");
        }

        if (state.ammo <= 0) {
            player.sendMessage("§eReload with /gun reload");
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
