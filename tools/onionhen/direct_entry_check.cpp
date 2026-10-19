/*
 * Opens AKENO's real plugin documents the way the patched host's direct entry does
 * (tools/onionhen/akeno-direct-entry.patch, docs/SHELLUI_DIRECT_ENTRY.md). It compiles onionHEN's own
 * plugin registry and dynamic page renderer (pinned v0.9.0 + the patch) on a PC and feeds them the
 * documents AKEN00001 1.10 sends (dumped by the companion's tests: every state, up to the largest
 * possible document):
 *
 *   OnionHEN?Plugin=AKEN00001 -> parse_plugin_page_uri -> direct_plugin_takes_resource(Settings root)
 *   -> render_plugin_root: the plugin's root page, identical to the page the Toolbox link opens
 *   -> every page reachable from there renders; every control on every page routes to the plugin
 *   -> leaving the pages ends the dynamic stack (Settings closes, the game is shown again)
 *
 * Firmware 12.20's profile is used. Nothing here runs on, or talks to, a console.
 *
 *   direct_entry_check <doc.bin> ...
 */
#include <onion/plugin_ui.hpp>

#include "dynamic_ui_runtime.hpp"
#include "dynamic_ui_xml.hpp"
#include "toolbox_route.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace onion;
namespace dyn = onion::shellui::dynamic_ui;

constexpr uint32_t kFirmware1220 = 0x12200000; /* the owner's console */
constexpr const char *kPluginId = "AKEN00001";
constexpr const char *kSettingsRoot = "debug_settings.xml";

int g_failures = 0;
std::string g_pressed;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::fprintf(stderr, "  %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            std::fprintf(stderr, __VA_ARGS__);                              \
            std::fprintf(stderr, "\n");                                     \
            g_failures++;                                                   \
            return false;                                                   \
        }                                                                   \
    } while (0)

bool sink(plugin_ui::Handle, const plugin_ui::Node &node, std::string_view, void *) {
    g_pressed = node.id;
    return true;
}

bool interactive(plugin_ui::NodeKind kind) {
    return kind == plugin_ui::NodeKind::Action || kind == plugin_ui::NodeKind::Toggle ||
           kind == plugin_ui::NodeKind::List || kind == plugin_ui::NodeKind::Input;
}

/* The page that contains `node` (its nearest Page ancestor). */
std::string page_of(const plugin_ui::Document &doc, const plugin_ui::Node &node) {
    const plugin_ui::Node *cursor = &node;
    while (cursor && !cursor->parent_id.empty()) {
        const plugin_ui::Node *parent = doc.find_node(cursor->parent_id);
        if (parent && parent->kind == plugin_ui::NodeKind::Page) return parent->id;
        cursor = parent;
    }
    return {};
}

/* Visits the pages reachable from `page` through Menu links, depth first, the way Settings pushes
 * them, and presses every control on each. */
bool walk(const plugin_ui::Document &doc, const std::string &page, std::set<std::string> &seen, int depth) {
    if (!seen.insert(page).second) return true;
    CHECK(depth < 16, "pages nested deeper than 16 (%s)", page.c_str());
    for (const plugin_ui::Node &node : doc.nodes) {
        if (page_of(doc, node) != page) continue;
        if (interactive(node.kind)) {
            g_pressed.clear();
            const dyn::DispatchResult r = dyn::dispatch_control(dyn::control_id(doc, node.id), node.value);
            CHECK(r == dyn::DispatchResult::Accepted && g_pressed == node.id, "press %s on %s not routed",
                  node.id.c_str(), page.c_str());
        }
        if (node.kind != plugin_ui::NodeKind::Menu || seen.count(node.target_id)) continue;
        std::string xml;
        CHECK(dyn::render_resource(dyn::resource_name(doc, node.target_id), xml), "page %s (from %s) does not render",
              node.target_id.c_str(), page.c_str());
        if (!walk(doc, node.target_id, seen, depth + 1)) return false;
        CHECK(dyn::leave_active_page(), "leaving %s did not return to %s", node.target_id.c_str(), page.c_str());
    }
    return true;
}

bool check(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(!bytes.empty(), "%s: empty", path.c_str());

    plugin_ui::Registry registry;
    const plugin_ui::RegisterResult reg = registry.register_encoded(kPluginId, bytes);
    CHECK(reg.status == plugin_ui::Status::Ok, "%s: onionHEN rejects the document: %s", path.c_str(), reg.error.c_str());
    const plugin_ui::RegistrySnapshot snapshot = registry.snapshot();
    const plugin_ui::Document &doc = *snapshot.find(reg.handle)->document;
    dyn::configure(kFirmware1220);
    dyn::replace_snapshot(snapshot);
    dyn::set_action_sink(sink, nullptr);

    /* The shortcut / game-menu URI names the plugin and takes over only the Settings root. */
    const std::string uri = toolbox::plugin_page_uri(kPluginId);
    CHECK(toolbox::parse_plugin_page_uri(uri) == kPluginId, "URI %s", uri.c_str());
    CHECK(toolbox::direct_plugin_takes_resource(kSettingsRoot, kSettingsRoot, kPluginId), "root takeover");
    CHECK(dyn::plugin_root_title(kPluginId) == doc.title, "game-menu title '%s'", dyn::plugin_root_title(kPluginId).c_str());

    /* The direct entry shows exactly the page the Toolbox link opens. */
    std::string direct, linked;
    CHECK(dyn::render_plugin_root(kPluginId, direct), "%s: no root page for the direct entry", path.c_str());
    CHECK(dyn::render_resource(dyn::resource_name(doc, doc.root_page_id), linked) && linked == direct,
          "%s: direct root differs from the linked root", path.c_str());

    std::set<std::string> seen;
    if (!walk(doc, doc.root_page_id, seen, 0)) return false;
    CHECK(!dyn::leave_active_page(), "%s: pages left on the stack after the root", path.c_str());

    size_t pages = 0;
    for (const plugin_ui::Node &node : doc.nodes) pages += node.kind == plugin_ui::NodeKind::Page;
    std::printf("%s: %zu nodes, %zu pages, %zu reachable from the direct entry\n", path.c_str(), doc.nodes.size(), pages,
                seen.size());
    dyn::replace_snapshot({});
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <doc.bin> ...\n", argv[0]);
        return 2;
    }
    int checked = 0;
    for (int i = 1; i < argc; i++) checked += check(argv[i]);
    std::printf("direct entry: %d of %d AKEN00001 documents open as the Settings root and route every press\n", checked,
                argc - 1);
    return g_failures == 0 && checked == argc - 1 ? 0 : 1;
}
