#include "stdafx.h"
#include "panorama_hook.h"
#include "config.h"
#include "item_schema.h"
#include "platform.h"

#include <regex>

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

std::string PanoramaHook::BuildCaseStoreData(const ItemSchema &itemSchema)
{
    using Kind = ItemSchema::ContainerKind;
    const GCConfig &config = GetConfig();
    auto kindEnabled = [&](Kind kind)
    {
        switch (kind)
        {
        case Kind::WeaponCase: return config.StoreWeaponCases();
        case Kind::SouvenirPackage: return config.StoreSouvenirPackages();
        case Kind::Capsule: return config.StoreCapsules();
        default: return config.StoreOtherContainers();
        }
    };

    std::vector<uint32_t> crates = itemSchema.GetContainerDefIndexes();
    std::stable_sort(crates.begin(), crates.end(), [&](uint32_t a, uint32_t b)
    {
        Kind kindA = itemSchema.GetContainerKind(a);
        Kind kindB = itemSchema.GetContainerKind(b);
        if (kindA != kindB)
        {
            return kindA < kindB;
        }
        return a > b; // newest first
    });

    std::string data = "[";
    bool first = true;
    for (uint32_t crate : crates)
    {
        Kind kind = itemSchema.GetContainerKind(crate);
        if (!kindEnabled(kind))
        {
            continue;
        }

        std::vector<uint32_t> keys = itemSchema.GetKeysForCrate(crate);
        uint32_t key = keys.empty() ? 0 : keys.front();

        if (!first)
        {
            data += ',';
        }
        first = false;

        data += '[';
        data += std::to_string(crate);
        data += ',';
        data += std::to_string(key);
        data += ',';
        data += std::to_string(static_cast<int>(kind));
        data += ']';
    }
    data += ']';
    return data;
}

// The Case Store screen. Built from code (the game's layout files can't be
// added to), run in the main menu's script context next to mainmenu.js.
static const char CaseStoreScriptPart1[] = R"js(
var CsgoGcCaseStore = ( function()
{
	var KIND_NAMES = [ 'Weapon Cases', 'Souvenir Packages', 'Capsules', 'Other' ];
	var BUY_AMOUNTS = [ 1, 5, 10 ];

	var m_data = [];
	var m_overlay = null;
	var m_grid = null;
	var m_search = null;
	var m_count = null;
	var m_status = null;
	var m_tabs = [];
	var m_tiles = [];
	var m_kind = -1;

	function _Style( panel, styles )
	{
		for ( var key in styles )
		{
			panel.style[ key ] = styles[ key ];
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

	function _MakeTile( entry )
	{
		var id = _FauxId( entry[ 0 ] );
		var keyId = entry[ 1 ] ? _FauxId( entry[ 1 ] ) : '';
		var kind = entry[ 2 ];
		var name = InventoryAPI.GetItemName( id ) || ( 'Item ' + entry[ 0 ] );

		var tile = _Panel( m_grid, {
			width: '230px', height: '300px', margin: '6px', flowChildren: 'down',
			backgroundColor: '#ffffff0c', border: '1px solid #ffffff16'
		} );

		var imageButton = _Panel( tile, { width: '100%', height: '160px' }, 'Button' );
		var image = _Panel( imageButton, { width: '100%', height: '100%' }, 'ItemImage', '', { scaling: 'stretch-to-fit-preserve-aspect' } );
		image.itemid = id;
		image.hittest = false;
		imageButton.SetPanelEvent( 'onactivate', function() { _ShowContents( id ); } );

		_Label( tile, name, { fontSize: '17px', color: '#ffffff', margin: '6px 10px 0px 10px', height: '46px' } );
		_Label( tile, ( KIND_NAMES[ kind ] || '' ) + ( keyId ? ' • key included' : '' ),
			{ fontSize: '13px', color: '#9aa4b1', margin: '0px 10px 0px 10px' } );

		var buttons = _Panel( tile, { flowChildren: 'right', horizontalAlign: 'center', marginTop: '10px' } );
		BUY_AMOUNTS.forEach( function( amount )
		{
			_Button( buttons, amount === 1 ? 'BUY' : 'x' + amount, {
				width: amount === 1 ? '86px' : '58px', height: '34px', margin: '0px 3px',
				backgroundColor: amount === 1 ? '#3d8a2c' : '#2d5d85'
			}, function() { _Buy( id, keyId, amount, name ); } );
		} );

		m_tiles.push( { panel: tile, name: name.toLowerCase(), kind: kind } );
	}
)js";

static const char CaseStoreScriptPart2[] = R"js(
	function _ApplyFilter()
	{
		var words = ( m_search ? m_search.text : '' ).toLowerCase().split( ' ' ).filter( function( w ) { return w.length > 0; } );
		var shown = 0;
		m_tiles.forEach( function( tile )
		{
			var visible = ( m_kind < 0 || tile.kind === m_kind ) && words.every( function( w ) { return tile.name.indexOf( w ) !== -1; } );
			tile.panel.visible = visible;
			if ( visible )
			{
				shown++;
			}
		} );

		m_tabs.forEach( function( tab )
		{
			tab.button.style.backgroundColor = tab.kind === m_kind ? '#2d5d85' : '#ffffff12';
		} );

		m_count.text = shown + ( shown === 1 ? ' item' : ' items' );
		m_grid.ScrollToTop();
	}

	function _SetKind( kind )
	{
		m_kind = kind;
		_ApplyFilter();
	}

	function _Build()
	{
		var context = $.GetContextPanel();
		m_overlay = _Panel( context, {
			width: '100%', height: '100%', flowChildren: 'down',
			padding: '36px 120px 16px 120px', backgroundColor: '#0c1016f4'
		}, 'Panel', 'CsgoGcCaseStore' );

		var popups = context.FindChild( 'PopupManager' );
		if ( popups )
		{
			context.MoveChildBefore( m_overlay, popups );
		}

		var header = _Panel( m_overlay, { width: '100%', height: '56px', flowChildren: 'right', marginBottom: '14px' } );
		_Label( header, 'CASE STORE', { fontSize: '38px', color: '#ffffff', fontWeight: 'bold', letterSpacing: '2px', verticalAlign: 'center', marginRight: '30px' } );

		m_search = _Panel( header, {
			width: '460px', height: '44px', verticalAlign: 'center', fontSize: '20px', color: '#ffffff',
			backgroundColor: '#ffffff14', border: '1px solid #ffffff30', padding: '8px 12px 0px 12px'
		}, 'TextEntry', 'CsgoGcCaseStoreSearch', { placeholder: 'Search...' } );
		m_search.SetPanelEvent( 'ontextentrychange', _ApplyFilter );

		var right = _Panel( header, { width: 'fill-parent-flow( 1.0 )', height: '100%' } );
		_Button( right, 'CLOSE', { horizontalAlign: 'right', verticalAlign: 'center', width: '120px', height: '42px', backgroundColor: '#ffffff18' }, _Close );

		var tabs = _Panel( m_overlay, { width: '100%', flowChildren: 'right', marginBottom: '10px' } );
		var counts = {};
		m_data.forEach( function( entry ) { counts[ entry[ 2 ] ] = ( counts[ entry[ 2 ] ] || 0 ) + 1; } );

		var tabKinds = [ -1 ];
		KIND_NAMES.forEach( function( name, kind ) { if ( counts[ kind ] ) tabKinds.push( kind ); } );
		tabKinds.forEach( function( kind )
		{
			var text = kind < 0 ? 'ALL (' + m_data.length + ')' : KIND_NAMES[ kind ].toUpperCase() + ' (' + counts[ kind ] + ')';
			var button = _Button( tabs, text, { height: '38px', marginRight: '8px', padding: '0px 18px', backgroundColor: '#ffffff12' },
				function() { _SetKind( kind ); }, { fontSize: '16px' } );
			m_tabs.push( { kind: kind, button: button } );
		} );

		var info = _Panel( m_overlay, { width: '100%', flowChildren: 'right', marginBottom: '6px' } );
		m_count = _Label( info, '', { fontSize: '15px', color: '#9aa4b1', marginRight: '24px' } );
		m_status = _Label( info, 'Click a case to see what\'s inside. Buy gets the case and its key.', { fontSize: '15px', color: '#c9d1d9' } );

		m_grid = _Panel( m_overlay, { width: '100%', height: 'fill-parent-flow( 1.0 )', flowChildren: 'right-wrap', overflow: 'squish scroll' } );
		m_data.forEach( _MakeTile );
		_ApplyFilter();
	}

	function _Open()
	{
		if ( !m_overlay )
		{
			_Build();
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
		button.SetPanelEvent( 'onmouseover', function() { UiToolkitAPI.ShowTextTooltip( 'CsgoGcCaseStoreNavBtn', 'Case Store' ); } );
		button.SetPanelEvent( 'onmouseout', function() { UiToolkitAPI.HideTextTooltip(); } );
	}

	function _SetData( data )
	{
		m_data = data || [];
		_AddNavButton();
	}

	return {
		SetData: _SetData,
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

RunScriptFn s_originalRunScript;
bool s_installDone;

void *s_uiEngine;
void *s_mainMenuPanel;
std::string s_caseStoreData;
bool s_caseStoreDataSent;
bool s_loggedContextPatch;

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
        s_originalRunScript(engine, edx, panel, PanoramaHook::CaseStoreScript().c_str(), CaseStoreScriptPath, 0, 0, 0, 0);
        Platform::Print("Panorama: Case Store added to the main menu\n");
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

    if (!GetConfig().CaseStoreMenu() && !GetConfig().GoldTradeUpRecipes())
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

void PanoramaHook::Tick()
{
    if (!s_originalRunScript || !s_mainMenuPanel || s_caseStoreDataSent || s_caseStoreData.empty())
    {
        return;
    }

    s_caseStoreDataSent = true;
    std::string script = "if ( typeof CsgoGcCaseStore !== 'undefined' ) CsgoGcCaseStore.SetData( "
        + s_caseStoreData + " );";
    s_originalRunScript(s_uiEngine, nullptr, s_mainMenuPanel, script.c_str(), CaseStoreDataPath, 0, 0, 0, 0);
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
