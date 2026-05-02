#include <endstone/endstone.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <optional>
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

        const auto &item = event.getItem();
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
    if (it == guns_.end()) return;

    auto &state = gun_states_[player.getName()];
    const auto now = steady_clock::now();
    if (state.last_shot.time_since_epoch().count() != 0 &&
        now - state.last_shot < it->second.fire_cooldown) return;

    if (state.ammo <= 0) {
        player.sendMessage("§cOut of ammo. /gun reload");
        return;
    }
    state.last_shot = now;
    state.ammo -= 1;

    auto feet  = player.getLocation();
    auto dir   = feet.getDirection();
    dir.normalize();

    // Ray origin = eye level
    const double ox  = feet.getX();
    const double oy  = feet.getY() + 1.62;
    const double oz  = feet.getZ();
    const double ddx = dir.getX();
    const double ddy = dir.getY();
    const double ddz = dir.getZ();

    player.playSound(feet, "random.explode", 1.0f, 1.5f);

    // --- DEBUG: แสดง ray info ---
    player.sendMessage("§8[DBG] eye=(" +
        std::to_string((int)ox) + "," +
        std::to_string((int)oy) + "," +
        std::to_string((int)oz) + ") dir=(" +
        std::to_string(ddx).substr(0,5) + "," +
        std::to_string(ddy).substr(0,5) + "," +
        std::to_string(ddz).substr(0,5) + ")");

    auto candidates = player.getServer().getOnlinePlayers();
    player.sendMessage("§8[DBG] online=" + std::to_string(candidates.size()));

    // --- Hit detection: perpendicular distance จาก point ถึง ray ---
    // แทนที่จะ march ทีละ step, คำนวณ exact closest distance แทน
    // สูตร: t = dot(P - O, D), perp = |(P - O) - t*D|
    endstone::Player *hit_target = nullptr;
    double hit_t       = 1e18;
    double hit_perp    = 1e18;

    for (auto *cand : candidates) {
        if (!cand || cand->getName() == player.getName()) continue;

        auto cloc = cand->getLocation();

        // ใช้ body center (feet + 1.0) แทนเท้า
        const double cx = cloc.getX() - ox;
        const double cy = (cloc.getY() + 1.0) - oy;
        const double cz = cloc.getZ() - oz;

        // Projection บน ray
        const double t = cx*ddx + cy*ddy + cz*ddz;

        // Perpendicular vector (closest point บน ray ถึง candidate)
        const double px = cx - t*ddx;
        const double py = cy - t*ddy;
        const double pz = cz - t*ddz;
        const double perp = std::sqrt(px*px + py*py + pz*pz);

        // DEBUG ทุก candidate
        player.sendMessage("§8[DBG] " + cand->getName() +
            " t=" + std::to_string((int)t) +
            " perp=" + std::to_string(perp).substr(0,5) +
            " pos=(" + std::to_string((int)cloc.getX()) + "," +
            std::to_string((int)cloc.getY()) + "," +
            std::to_string((int)cloc.getZ()) + ")");

        // ต้องอยู่ข้างหน้า (t > 0) และในระยะ และ perp น้อยพอ
        if (t < 0.5 || t > it->second.range) continue;
        if (perp > 1.2) continue; // generous radius ก่อน

        // เลือก closest ตาม t
        if (t < hit_t) {
            hit_t      = t;
            hit_perp   = perp;
            hit_target = cand;
        }
    }

    if (hit_target) {
        int new_hp = std::max(0, hit_target->getHealth() - it->second.damage);
        hit_target->setHealth(new_hp);
        player.sendMessage("§a[HIT] §f" + hit_target->getName() +
                           " §c-" + std::to_string(it->second.damage) + "HP" +
                           " §8(t=" + std::to_string((int)hit_t) +
                           " perp=" + std::to_string(hit_perp).substr(0,5) + ")");
        hit_target->sendMessage("§c[SHOT] §fYou were hit by " + player.getName());
        auto hloc = hit_target->getLocation();
        hloc.setY(hloc.getY() + 1.0);
        player.spawnParticle("minecraft:explosion_particle", hloc);
    } else {
        player.sendMessage("§7[MISS]");
    }

    if (state.ammo <= 0) player.sendMessage("§eReload with /gun reload");
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
