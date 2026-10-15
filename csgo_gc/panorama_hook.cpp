#include "stdafx.h"
#include "panorama_hook.h"
#include "config.h"
#include "item_schema.h"
#include "platform.h"
#include "star_shop.h"

#include <regex>
#include <unordered_set>

// ============================================================================
// text processing (all platforms)
// ============================================================================

int PanoramaHook::PatchItemContextEntries(std::string &source)
{
    // the "Add to contract" / "Remove from contract" entries skip knives and
    // gloves:  slot !=="melee" && slot !=="c4" && slot !=="clothing_hands" &&
    //          !ItemInfo.IsEquippalbleButNotAWeapon(id)
    static const std::regex slotCheck{
        R"re(slot\s*!==\s*"melee"\s*&&\s*slot\s*!==\s*"c4"\s*&&\s*slot\s*!==\s*"clothing_hands"\s*&&\s*!\s*ItemInfo\.IsEquippalbleButNotAWeapon\(\s*id\s*\))re" };

    int count = 0;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), slotCheck); it != std::sregex_iterator(); ++it)
    {
        count++;
    }

    if (count)
    {
        source = std::regex_replace(source, slotCheck, R"(slot !=="c4")");
    }

    return count;
}

static void AppendJsString(std::string &out, std::string_view text)
{
    out += '"';
    for (char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        if (static_cast<unsigned char>(c) >= 0x20)
        {
            out += c;
        }
    }
    out += '"';
}

std::string PanoramaHook::BuildCaseStoreData(const ItemSchema &itemSchema)
{
    const StarShopConfig &starShop = GetConfig().StarShop();
    const bool priced = starShop.enabled && itemSchema.LatestSeasonalOperation();

    // [defIndex, keyDefIndex or 0, kind, star price, rarity, custom name]
    // kinds: 0 weapon case, 1 souvenir package, 2 capsule, 3 other, 4 agent, 5 custom case
    std::string data = "[";
    bool first = true;
    auto begin = [&]()
    {
        if (!first)
        {
            data += ',';
        }
        first = false;
        data += '[';
    };

    for (uint32_t crate : StarShop::ShopContainers(itemSchema, starShop))
    {
        std::vector<uint32_t> keys = itemSchema.GetKeysForCrate(crate);
        const bool custom = itemSchema.IsCustomCase(crate);
        const int kind = custom ? 5 : static_cast<int>(itemSchema.GetContainerKind(crate));

        begin();
        data += std::to_string(crate) + ',' + std::to_string(keys.empty() ? 0 : keys.front()) + ','
            + std::to_string(kind) + ',' + std::to_string(priced ? StarShop::Price(itemSchema, starShop, crate) : 0)
            + ",0,";
        const std::string *customName = custom ? itemSchema.CustomCaseName(crate) : nullptr;
        AppendJsString(data, customName ? std::string_view{ *customName } : std::string_view{});
        data += ']';
    }

    // agents, rarest first
    if (priced)
    {
        std::vector<uint32_t> agents = itemSchema.GetAgentDefIndexes();
        std::stable_sort(agents.begin(), agents.end(), [&](uint32_t a, uint32_t b)
        {
            const ItemInfo *infoA = itemSchema.ItemInfoByDefIndex(a);
            const ItemInfo *infoB = itemSchema.ItemInfoByDefIndex(b);
            return (infoA ? infoA->m_rarity : 0) > (infoB ? infoB->m_rarity : 0);
        });

        for (uint32_t agent : agents)
        {
            const ItemInfo *info = itemSchema.ItemInfoByDefIndex(agent);
            begin();
            data += std::to_string(agent) + ",0,4," + std::to_string(starShop.agentPrice) + ','
                + std::to_string(info ? info->m_rarity : 0) + ",\"\"]";
        }
    }

    data += ']';
    return data;
}

// The shop screen. Built from code (the game's layout files can't be added
// to), run in the main menu's script context next to mainmenu.js.
static const char CaseStoreScriptPart1[] = R"js(
var CsgoGcCaseStore = ( function()
{
	// [ kind, title ] in the order the sections are shown
	var SECTIONS = [
		[ 5, 'CUSTOM CASES' ], [ 4, 'AGENTS' ], [ 0, 'WEAPON CASES' ],
		[ 1, 'SOUVENIR PACKAGES' ], [ 2, 'CAPSULES' ], [ 3, 'OTHER' ]
	];
	var KIND_NAMES = [ 'Weapon Case', 'Souvenir Package', 'Capsule', 'Other', 'Agent', 'Custom Case' ];
	var RARITY_COLORS = { 1: '#b0c3d9', 2: '#5e98d9', 3: '#4b69ff', 4: '#8847ff', 5: '#d32ce6', 6: '#eb4b4b', 7: '#e4ae39' };
	var BUY_AMOUNTS = [ 1, 5, 10 ];
	var GOLD = '#f5c542';

	var m_data = [];
	var m_overlay = null;
	var m_scroll = null;
	var m_search = null;
	var m_count = null;
	var m_status = null;
	var m_balance = null;
	var m_balanceBox = null;
	var m_stars = -1;
	var m_tiles = [];
	var m_sections = [];

	function _Style( panel, styles )
	{
		for ( var key in styles )
		{
			try
			{
				panel.style[ key ] = styles[ key ];
			}
			catch ( e )
			{
				$.Msg( '[csgo_gc] shop: bad style ' + key + ': ' + e );
			}
		}
		return panel;
	}

	function _Panel( parent, styles, type, id, props )
	{
		return _Style( $.CreatePanel( type || 'Panel', parent, id || '', props || {} ), styles || {} );
	}

	function _Label( parent, text, styles )
	{
		var label = _Panel( parent, styles, 'Label' );
		label.text = text;
		return label;
	}

	function _Button( parent, text, styles, onActivate, labelStyles )
	{
		var button = _Panel( parent, styles, 'Button' );
		var labelStyle = { color: '#ffffff', fontSize: '17px', horizontalAlign: 'center', verticalAlign: 'center' };
		for ( var key in ( labelStyles || {} ) )
		{
			labelStyle[ key ] = labelStyles[ key ];
		}
		_Label( button, text, labelStyle );
		button.SetPanelEvent( 'onactivate', onActivate );
		return button;
	}

	function _FauxId( defIndex )
	{
		return InventoryAPI.GetFauxItemIDFromDefAndPaintIndex( defIndex, 0 );
	}

	function _Buy( id, keyId, amount, name )
	{
		var ids = [];
		for ( var i = 0; i < amount; i++ )
		{
			ids.push( id );
			if ( keyId )
			{
				ids.push( keyId );
			}
		}

		StoreAPI.StoreItemPurchase( ids.join( ',' ) );
		m_status.text = 'Buying ' + amount + ' x ' + name + ( keyId ? ( amount > 1 ? ' with keys' : ' with key' ) : '' ) + '...';
	}

	function _ShowContents( id )
	{
		UiToolkitAPI.ShowCustomLayoutPopupParameters(
			'popup-inspect-' + id,
			'file://{resources}/layout/popups/popup_capability_decodable.xml',
			'key-and-case=,' + id
			+ '&asyncworkitemwarning=no'
			+ '&asyncforcehide=true'
			+ '&storeitemid=' + id
			+ '&overridepurchasemultiple=1'
		);
	}

	// dims the buy buttons that cost more than the balance
	function _ShowAffordability()
	{
		m_tiles.forEach( function( tile )
		{
			tile.buttons.forEach( function( entry )
			{
				var affordable = m_stars < 0 || tile.price * entry.amount <= m_stars;
				entry.button.style.opacity = affordable ? '1.0' : '0.35';
			} );
		} );
	}

	function _MakeTile( grid, entry )
	{
		var kind = entry[ 2 ];
		var price = entry[ 3 ];
		var rarity = entry[ 4 ];
		var id = _FauxId( entry[ 0 ] );
		var keyId = entry[ 1 ] ? _FauxId( entry[ 1 ] ) : '';
		var name = entry[ 5 ] || InventoryAPI.GetItemName( id ) || ( 'Item ' + entry[ 0 ] );
		var isAgent = kind === 4;

		var tile = _Panel( grid, {
			width: '218px', height: '286px', margin: '0px 10px 12px 0px', flowChildren: 'down',
			backgroundColor: '#ffffff12'
		} );

		var imageButton = _Panel( tile, { width: '100%', height: '150px' }, 'Button' );
		var image = _Panel( imageButton, { width: '100%', height: '100%' }, 'ItemImage', '', { scaling: 'stretch-to-fit-preserve-aspect' } );
		image.itemid = id;
		image.hittest = false;
		if ( !isAgent )
		{
			imageButton.SetPanelEvent( 'onactivate', function() { _ShowContents( id ); } );
		}

		// the rarity colour under the picture
		_Panel( tile, { width: '100%', height: '5px', backgroundColor: isAgent ? ( RARITY_COLORS[ rarity ] || '#b0c3d9' ) : '#ffffff22' } );

		_Label( tile, name, { fontSize: '16px', color: '#ffffff', margin: '8px 10px 0px 10px', height: '40px' } );

		var info = _Panel( tile, { width: '100%', flowChildren: 'right', margin: '0px 10px 0px 10px' } );
		_Label( info, ( KIND_NAMES[ kind ] || '' ) + ( keyId ? ' + key' : '' ), { fontSize: '12px', color: '#9aa4b1', verticalAlign: 'center' } );
		_Label( info, price > 0 ? ( price + ' stars' ) : 'free', { fontSize: '17px', color: GOLD, fontWeight: 'bold', horizontalAlign: 'right', marginRight: '20px' } );

		var buttonRow = _Panel( tile, { flowChildren: 'right', horizontalAlign: 'center', marginTop: '10px' } );
		var buttons = [];
		BUY_AMOUNTS.forEach( function( amount )
		{
			var button = _Button( buttonRow, amount === 1 ? 'BUY' : 'x' + amount, {
				width: amount === 1 ? '80px' : '54px', height: '32px', margin: '0px 3px',
				backgroundColor: amount === 1 ? '#3d8a2c' : '#2d5d85'
			}, function() { _Buy( id, keyId, amount, name ); }, { fontSize: '16px' } );
			buttons.push( { button: button, amount: amount } );
		} );

		var record = { panel: tile, name: name.toLowerCase(), kind: kind, price: price, buttons: buttons };
		m_tiles.push( record );
		return record;
	}
)js";

static const char CaseStoreScriptPart2[] = R"js(
	function _ApplyFilter()
	{
		var words = ( m_search ? m_search.text : '' ).toLowerCase().split( ' ' ).filter( function( w ) { return w.length > 0; } );
		var shown = 0;
		m_sections.forEach( function( section )
		{
			var visibleInSection = 0;
			section.tiles.forEach( function( tile )
			{
				var visible = words.every( function( w ) { return tile.name.indexOf( w ) !== -1; } );
				tile.panel.visible = visible;
				if ( visible )
				{
					visibleInSection++;
				}
			} );

			section.panel.visible = visibleInSection > 0;
			section.title.text = section.name + ' (' + visibleInSection + ')';
			shown += visibleInSection;
		} );

		m_count.text = shown + ( shown === 1 ? ' item' : ' items' );
		m_scroll.ScrollToTop();
	}

	function _ShowStars()
	{
		if ( m_balanceBox )
		{
			m_balanceBox.visible = m_stars >= 0;
			m_balance.text = m_stars >= 0 ? ( 'Your Balance:  ' + m_stars + ' stars' ) : '';
		}
		_ShowAffordability();
	}

	function _SetStars( stars )
	{
		m_stars = stars;
		_ShowStars();
	}

	function _Build()
	{
		var context = $.GetContextPanel();
		m_overlay = _Panel( context, {
			width: '100%', height: '100%', flowChildren: 'down',
			padding: '0px 0px 0px 0px', backgroundColor: '#120d0cf6'
		}, 'Panel', 'CsgoGcCaseStore' );

		var popups = context.FindChild( 'PopupManager' );
		if ( popups )
		{
			context.MoveChildBefore( m_overlay, popups );
		}

		// title, subtitle and balance
		var header = _Panel( m_overlay, { width: '100%', height: '92px', flowChildren: 'right', padding: '30px 120px 0px 120px', borderBottom: '1px solid #ffffff30' } );
		var titles = _Panel( header, { flowChildren: 'down', width: 'fill-parent-flow( 1.0 )' } );
		_Label( titles, 'SkinStop', { fontSize: '34px', color: '#ffffff', fontWeight: 'bold' } );
		_Label( titles, 'Use your Stars to get Rewards of your choosing.', { fontSize: '16px', color: '#c9d1d9' } );

		m_balanceBox = _Panel( header, { height: '40px', verticalAlign: 'center', padding: '0px 18px', backgroundColor: '#3a2f1a', border: '1px solid #6b5a2e' } );
		m_balanceBox.visible = false;
		m_balance = _Label( m_balanceBox, '', { fontSize: '20px', color: GOLD, fontWeight: 'bold', verticalAlign: 'center' } );

		// search and messages
		var bar = _Panel( m_overlay, { width: '100%', flowChildren: 'right', padding: '14px 120px 8px 120px' } );
		m_search = _Panel( bar, {
			width: '420px', height: '40px', verticalAlign: 'center', fontSize: '19px', color: '#ffffff',
			backgroundColor: '#ffffff14', border: '1px solid #ffffff30', padding: '6px 12px 0px 12px', marginRight: '24px'
		}, 'TextEntry', 'CsgoGcCaseStoreSearch', { placeholder: 'Search...' } );
		m_search.SetPanelEvent( 'ontextentrychange', _ApplyFilter );
		m_count = _Label( bar, '', { fontSize: '15px', color: '#9aa4b1', verticalAlign: 'center', marginRight: '24px' } );
		m_status = _Label( bar, 'Click a case to see what\'s inside. A case comes with its key.', { fontSize: '15px', color: '#c9d1d9', verticalAlign: 'center' } );

		// the sections
		m_scroll = _Panel( m_overlay, { width: '100%', height: 'fill-parent-flow( 1.0 )', flowChildren: 'down', overflow: 'squish scroll', padding: '4px 120px 0px 120px' } );
		SECTIONS.forEach( function( definition )
		{
			var entries = m_data.filter( function( entry ) { return entry[ 2 ] === definition[ 0 ]; } );
			if ( !entries.length )
			{
				return;
			}

			var panel = _Panel( m_scroll, { width: '100%', height: 'fit-children', flowChildren: 'down', padding: '16px 16px 4px 16px', margin: '0px 0px 18px 0px', backgroundColor: '#ffffff0c' } );
			var title = _Label( panel, '', { fontSize: '17px', color: '#c9d1d9', letterSpacing: '3px', marginBottom: '12px' } );
			var grid = _Panel( panel, { width: '100%', height: 'fit-children', flowChildren: 'right-wrap' } );

			var section = { panel: panel, title: title, name: definition[ 1 ], tiles: [] };
			entries.forEach( function( entry ) { section.tiles.push( _MakeTile( grid, entry ) ); } );
			m_sections.push( section );
		} );

		// back button along the bottom
		var footer = _Panel( m_overlay, { width: '100%', height: '60px', borderTop: '1px solid #ffffff30' } );
		_Button( footer, 'BACK', { horizontalAlign: 'right', verticalAlign: 'center', marginRight: '120px', width: '130px', height: '40px', backgroundColor: '#ffffff18' }, _Close, { fontSize: '19px' } );

		_ShowStars();
		_ApplyFilter();
	}

	function _Open()
	{
		if ( !m_overlay )
		{
			try
			{
				_Build();
			}
			catch ( e )
			{
				$.Msg( '[csgo_gc] shop: building the screen failed: ' + e );
				if ( m_overlay )
				{
					_Label( m_overlay, 'The shop could not be built: ' + e, { fontSize: '18px', color: '#ff8080', margin: '40px 120px' } );
					_Button( m_overlay, 'CLOSE', { margin: '0px 120px', width: '130px', height: '40px', backgroundColor: '#ffffff18' }, _Close );
				}
			}
		}

		if ( !m_search )
		{
			return;
		}

		m_overlay.visible = true;
		m_search.SetFocus();
	}

	function _Close()
	{
		if ( m_overlay )
		{
			m_overlay.visible = false;
		}
	}

	function _Toggle()
	{
		if ( m_overlay && m_overlay.visible )
		{
			_Close();
		}
		else
		{
			_Open();
		}
	}

	function _AddNavButton()
	{
		var context = $.GetContextPanel();
		var nav = context.FindChildTraverse( 'JsMainMenuNavBar' );
		if ( !nav || nav.FindChildTraverse( 'CsgoGcCaseStoreNavBtn' ) )
		{
			return;
		}

		var button = $.CreatePanel( 'Button', nav, 'CsgoGcCaseStoreNavBtn', { class: 'mainmenu-navbar__btn-small MainMenuModeOnly' } );
		$.CreatePanel( 'Image', button, '', { src: 'file://{images}/icons/ui/shoppingcart.svg', textureheight: '32', texturewidth: '-1' } );

		var inventory = nav.FindChildTraverse( 'MainMenuNavBarInventory' );
		if ( inventory && inventory.GetParent() === nav )
		{
			nav.MoveChildAfter( button, inventory );
		}

		button.SetPanelEvent( 'onactivate', _Toggle );
		button.SetPanelEvent( 'onmouseover', function() { UiToolkitAPI.ShowTextTooltip( 'CsgoGcCaseStoreNavBtn', 'SkinStop' ); } );
		button.SetPanelEvent( 'onmouseout', function() { UiToolkitAPI.HideTextTooltip(); } );
	}

	function _SetData( data )
	{
		m_data = data || [];
		_AddNavButton();
	}

	return {
		SetData: _SetData,
		SetStars: _SetStars,
		Open: _Open,
		Close: _Close
	};
} )();
)js";

const std::string &PanoramaHook::CaseStoreScript()
{
    static const std::string script = std::string{ CaseStoreScriptPart1 } + CaseStoreScriptPart2;
    return script;
}

// ============================================================================
// the hook (Windows)
// ============================================================================

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <funchook.h>

namespace
{

// CUIEngine::RunScript (compile+run): thiscall, 7 stack args, ret 0x1c.
// every <include src="...js"/> of a layout file is run through it
using RunScriptFn = void(__fastcall *)(void *engine, void *edx, void *panel, const char *source,
    const char *path, int line, int column, int arg6, int arg7);

constexpr char RunScriptString[] = "CUIEngine::RunScript (compile+run)";
// push ebp; mov ebp, esp; and esp, -8; sub esp, imm32
constexpr uint8_t RunScriptPrologue[] = { 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec };

constexpr char CaseStoreScriptPath[] = "file://{resources}/scripts/csgo_gc_case_store.js";
constexpr char CaseStoreDataPath[] = "file://{resources}/scripts/csgo_gc_case_store_data.js";

// panorama_debug: the UI scripts are saved to csgo_gc/panorama_dump/ and a few
// questions about the operation are asked and logged, so it's clear why the
// operation page does or doesn't show
std::unordered_set<std::string> s_dumpedScripts;
std::chrono::steady_clock::time_point s_mainMenuLoadedAt;
int s_diagnosticRuns;
bool s_diagnosticsPending;

void DumpScript(const char *path, const char *source)
{
    if (!s_dumpedScripts.insert(path).second)
    {
        return;
    }

    std::string name;
    for (const char *c = path; *c; ++c)
    {
        name += (isalnum(static_cast<unsigned char>(*c)) || *c == '.' || *c == '-') ? *c : '_';
    }
    if (name.size() > 150)
    {
        name = name.substr(name.size() - 150);
    }

    CreateDirectoryA("csgo_gc", nullptr);
    CreateDirectoryA("csgo_gc/panorama_dump", nullptr);
    std::string file = "csgo_gc/panorama_dump/" + name;
    if (FILE *f = fopen(file.c_str(), "wb"))
    {
        fwrite(source, 1, strlen(source), f);
        fclose(f);
    }
}

RunScriptFn s_originalRunScript;
bool s_installDone;

void *s_uiEngine;
void *s_mainMenuPanel;
std::string s_caseStoreData;
bool s_caseStoreDataSent;
bool s_loggedContextPatch;
int64_t s_starsSent = -2; // what the UI was last told

struct Section
{
    const uint8_t *start;
    size_t size;
};

bool GetSection(HMODULE module, const char *name, Section &out)
{
    auto base = reinterpret_cast<const uint8_t *>(module);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return false;
    }

    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
    {
        return false;
    }

    const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
    {
        if (strncmp(reinterpret_cast<const char *>(section->Name), name, IMAGE_SIZEOF_SHORT_NAME) == 0)
        {
            out.start = base + section->VirtualAddress;
            out.size = section->Misc.VirtualSize;
            return true;
        }
    }

    return false;
}

// unique match or null
const uint8_t *FindUnique(const Section &section, const uint8_t *pattern, size_t patternSize)
{
    const uint8_t *found = nullptr;
    for (size_t offset = 0; offset + patternSize <= section.size; offset++)
    {
        if (section.start[offset] == pattern[0] && memcmp(section.start + offset, pattern, patternSize) == 0)
        {
            if (found)
            {
                return nullptr;
            }
            found = section.start + offset;
        }
    }
    return found;
}

const uint8_t *FindRunScript(HMODULE panorama)
{
    Section text, rdata;
    if (!GetSection(panorama, ".text", text) || !GetSection(panorama, ".rdata", rdata))
    {
        return nullptr;
    }

    // the function's profiling scope name, and the "push offset string" using it
    const uint8_t *string = FindUnique(rdata, reinterpret_cast<const uint8_t *>(RunScriptString), sizeof(RunScriptString));
    if (!string)
    {
        return nullptr;
    }

    uint8_t push[5] = { 0x68 };
    const uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(string));
    memcpy(push + 1, &address, sizeof(address));
    const uint8_t *reference = FindUnique(text, push, sizeof(push));
    if (!reference)
    {
        return nullptr;
    }

    // walk back to the function start (after int3/nop padding)
    for (const uint8_t *p = reference; p > text.start && reference - p < 0x200; p--)
    {
        if ((p[-1] == 0xcc || p[-1] == 0x90 || p[-1] == 0xc3)
            && memcmp(p, RunScriptPrologue, sizeof(RunScriptPrologue)) == 0)
        {
            return p;
        }
    }

    return nullptr;
}

bool PathIsFile(const char *path, const char *fileName)
{
    size_t pathLength = strlen(path);
    size_t nameLength = strlen(fileName);
    if (pathLength < nameLength + 1)
    {
        return false;
    }

    const char *tail = path + pathLength - nameLength;
    char separator = tail[-1];
    return (separator == '/' || separator == '\\') && _stricmp(tail, fileName) == 0;
}

void __fastcall Hk_RunScript(void *engine, void *edx, void *panel, const char *source,
    const char *path, int line, int column, int arg6, int arg7)
{
    if (!source || !path || !panel)
    {
        s_originalRunScript(engine, edx, panel, source, path, line, column, arg6, arg7);
        return;
    }

    if (GetConfig().PanoramaDebug())
    {
        DumpScript(path, source);
    }

    if (GetConfig().GoldTradeUpRecipes() && PathIsFile(path, "item_context_entries.js"))
    {
        std::string patched = source;
        int changes = PanoramaHook::PatchItemContextEntries(patched);
        if (!s_loggedContextPatch)
        {
            s_loggedContextPatch = true;
            Platform::Print(changes
                    ? "Panorama: knives and gloves can be added to trade-up contracts\n"
                    : "Panorama: item_context_entries.js didn't match, knives can't be added to contracts from the menu\n");
        }

        if (changes)
        {
            s_originalRunScript(engine, edx, panel, patched.c_str(), path, line, column, arg6, arg7);
            return;
        }
    }

    s_originalRunScript(engine, edx, panel, source, path, line, column, arg6, arg7);

    if (GetConfig().CaseStoreMenu() && PathIsFile(path, "mainmenu.js"))
    {
        s_uiEngine = engine;
        s_mainMenuPanel = panel;
        s_caseStoreDataSent = false;
        s_starsSent = -2;
        s_originalRunScript(engine, edx, panel, PanoramaHook::CaseStoreScript().c_str(), CaseStoreScriptPath, 0, 0, 0, 0);
        Platform::Print("Panorama: Case Store added to the main menu\n");
        s_mainMenuLoadedAt = std::chrono::steady_clock::now();
        s_diagnosticRuns = 0;
        s_diagnosticsPending = GetConfig().PanoramaDebug();
        PanoramaHook::Tick();
    }
}

} // namespace

void PanoramaHook::TryInstall()
{
    if (s_installDone)
    {
        return;
    }

    if (!GetConfig().CaseStoreMenu() && !GetConfig().GoldTradeUpRecipes() && !GetConfig().PanoramaDebug())
    {
        s_installDone = true;
        return;
    }

    HMODULE panorama = GetModuleHandleA("panorama.dll");
    if (!panorama)
    {
        return; // not loaded yet (or a dedicated server)
    }

    s_installDone = true;

    const uint8_t *function = FindRunScript(panorama);
    if (!function)
    {
        Platform::Print("Panorama: this panorama.dll build is not supported, no Case Store menu\n");
        return;
    }

    funchook_t *funchook = funchook_create();
    if (!funchook)
    {
        Platform::Print("Panorama: funchook_create failed\n");
        return;
    }

    void *target = const_cast<uint8_t *>(function);
    if (funchook_prepare(funchook, &target, reinterpret_cast<void *>(&Hk_RunScript)) != 0)
    {
        Platform::Print("Panorama: could not prepare the hook (%s)\n", funchook_error_message(funchook));
        funchook_destroy(funchook);
        return;
    }

    s_originalRunScript = reinterpret_cast<RunScriptFn>(target);

    if (funchook_install(funchook, 0) != 0)
    {
        Platform::Print("Panorama: could not install the hook (%s)\n", funchook_error_message(funchook));
        s_originalRunScript = nullptr;
        funchook_destroy(funchook);
        return;
    }

    Platform::Print("Panorama: UI hook installed\n");
}

void PanoramaHook::SetCaseStoreData(std::string data)
{
    s_caseStoreData = std::move(data);
    s_caseStoreDataSent = false;
}

bool PanoramaHook::HasCaseStoreData()
{
    return !s_caseStoreData.empty();
}

// logs what the game thinks about the operation, to the console
static const char DiagnosticScript[] = R"js(
( function()
{
	function log( text ) { $.Msg( '[csgo_gc] ' + text ); }
	function ask( name, fn )
	{
		try { log( name + ' = ' + fn() ); }
		catch ( e ) { log( name + ' failed: ' + e ); }
	}

	ask( 'MyPersonaAPI.GetActiveSeasonIndex', function() { return MyPersonaAPI.GetActiveSeasonIndex(); } );
	ask( 'MyPersonaAPI.GetActiveSeasonAccess', function() { return MyPersonaAPI.GetActiveSeasonAccess(); } );
	ask( 'MyPersonaAPI.GetActiveSeasonCoinRank', function() { return MyPersonaAPI.GetActiveSeasonCoinRank(); } );
	ask( 'MyPersonaAPI.GetActiveSeasonPassTime', function() { return MyPersonaAPI.GetActiveSeasonPassTime(); } );
	ask( 'MyPersonaAPI.GetActiveSeasonRedeemableBalance', function() { return MyPersonaAPI.GetActiveSeasonRedeemableBalance(); } );
	ask( 'GameTypesAPI.GetActiveSeasonIndex', function() { return GameTypesAPI.GetActiveSeasonIndex(); } );
	ask( 'typeof OperationUtil', function() { return typeof OperationUtil; } );
	ask( 'typeof MissionsAPI', function() { return typeof MissionsAPI; } );

	var nav = $.GetContextPanel().FindChildTraverse( 'JsMainMenuNavBar' );
	if ( !nav )
	{
		log( 'no JsMainMenuNavBar' );
		return;
	}

	for ( var i = 0; i < nav.GetChildCount(); i++ )
	{
		var child = nav.GetChild( i );
		log( 'navbar child ' + i + ': id=' + child.id + ' visible=' + child.visible );
	}

	[ 'MainMenuNavBarOperation', 'JsOperation', 'id-op-mainmenu-top', 'id-op-mainmenu-rewards', 'id-op-mainmenu-missions' ].forEach( function( id )
	{
		var panel = $.GetContextPanel().FindChildTraverse( id );
		log( 'panel ' + id + ': ' + ( panel ? ( 'found, visible=' + panel.visible ) : 'not found' ) );
	} );
} )();
)js";

void PanoramaHook::Tick()
{
    if (!s_originalRunScript || !s_mainMenuPanel)
    {
        return;
    }

    // the answers change once the game has its account data, so ask a few times
    if (s_diagnosticsPending)
    {
        const auto sinceLoad = std::chrono::steady_clock::now() - s_mainMenuLoadedAt;
        const int dueRuns = sinceLoad > std::chrono::seconds{ 45 } ? 3 : sinceLoad > std::chrono::seconds{ 20 } ? 2
            : sinceLoad > std::chrono::seconds{ 5 } ? 1 : 0;
        if (s_diagnosticRuns < dueRuns)
        {
            s_diagnosticRuns = dueRuns;
            s_originalRunScript(s_uiEngine, nullptr, s_mainMenuPanel, DiagnosticScript,
                "file://{resources}/scripts/csgo_gc_diagnostics.js", 0, 0, 0, 0);
            if (s_diagnosticRuns >= 3)
            {
                s_diagnosticsPending = false;
            }
        }
    }

    if (!s_caseStoreDataSent && !s_caseStoreData.empty())
    {
        s_caseStoreDataSent = true;
        s_starsSent = -2;
        std::string script = "if ( typeof CsgoGcCaseStore !== 'undefined' ) CsgoGcCaseStore.SetData( "
            + s_caseStoreData + " );";
        s_originalRunScript(s_uiEngine, nullptr, s_mainMenuPanel, script.c_str(), CaseStoreDataPath, 0, 0, 0, 0);
    }

    const int64_t stars = StarShop::DisplayBalance();
    if (s_caseStoreDataSent && stars != s_starsSent)
    {
        s_starsSent = stars;
        std::string script = "if ( typeof CsgoGcCaseStore !== 'undefined' ) CsgoGcCaseStore.SetStars( "
            + std::to_string(stars) + " );";
        s_originalRunScript(s_uiEngine, nullptr, s_mainMenuPanel, script.c_str(), CaseStoreDataPath, 0, 0, 0, 0);
    }
}

#else

void PanoramaHook::TryInstall()
{
}

void PanoramaHook::SetCaseStoreData(std::string)
{
}

bool PanoramaHook::HasCaseStoreData()
{
    return true;
}

void PanoramaHook::Tick()
{
}

#endif
