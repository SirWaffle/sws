/******************************************************************************
/ SnM_LiveConfigs.cpp
/
/ Copyright (c) 2010-2013 Jeffos
/
/
/ Permission is hereby granted, free of charge, to any person obtaining a copy
/ of this software and associated documentation files (the "Software"), to deal
/ in the Software without restriction, including without limitation the rights to
/ use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
/ of the Software, and to permit persons to whom the Software is furnished to
/ do so, subject to the following conditions:
/ 
/ The above copyright notice and this permission notice shall be included in all
/ copies or substantial portions of the Software.
/ 
/ THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
/ EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
/ OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
/ NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
/ HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
/ WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
/ FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
/ OTHER DEALINGS IN THE SOFTWARE.
/
******************************************************************************/

#include "stdafx.h"
#include "SnM.h"
#include "SnM_Chunk.h"
#include "SnM_Dlg.h"
#include "SnM_FX.h"
#include "SnM_LiveConfigs.h"
#include "SnM_Routing.h"
#include "SnM_Track.h"
#include "SnM_Util.h"
#include "SnM_Window.h"
#include "../reaper/localize.h"
#include "../Prompt.h"
#include "../../WDL/projectcontext.h"


#define LIVECFG_WND_ID				"SnMLiveConfigs"
#define LIVECFG_MON_WND_ID			"SnMLiveConfigMonitor%d"
#define LIVECFG_VERSION				2

//#define UNUSED					0xF001 
#define CLEAR_CC_ROW_MSG			0xF002
#define CLEAR_TRACK_MSG				0xF003
#define CLEAR_FXCHAIN_MSG			0xF004
#define CLEAR_TRACK_TEMPLATE_MSG	0xF005
#define CLEAR_DESC_MSG				0xF006
#define CLEAR_PRESETS_MSG			0xF007
#define CLEAR_ON_ACTION_MSG			0xF008
#define CLEAR_OFF_ACTION_MSG		0xF009 
#define LOAD_TRACK_TEMPLATE_MSG		0xF00A
#define LOAD_FXCHAIN_MSG			0xF00B
#define EDIT_DESC_MSG				0xF00C
#define EDIT_ON_ACTION_MSG			0xF00D
#define EDIT_OFF_ACTION_MSG			0xF00E
#define LEARN_ON_ACTION_MSG			0xF00F
#define LEARN_OFF_ACTION_MSG		0xF010
#define MUTE_OTHERS_MSG				0xF011
#define SELSCROLL_MSG				0xF012
#define OFFLINE_OTHERS_MSG			0xF013
#define CC123_MSG					0xF014
#define IGNORE_EMPTY_MSG			0xF015
#define AUTOSENDS_MSG				0xF016
#define LEARN_APPLY_MSG				0xF017
#define LEARN_PRELOAD_MSG			0xF018
#define HELP_MSG					0xF019
#define APPLY_MSG					0xF01A
#define PRELOAD_MSG					0xF01B
#define SHOW_FX_MSG					0xF01C
#define SHOW_IO_MSG					0xF01D
#define SHOW_FX_INPUT_MSG			0xF01E
#define SHOW_IO_INPUT_MSG			0xF01F
#define INS_UP_MSG					0xF020
#define INS_DOWN_MSG				0xF021
#define CUT_MSG						0xF022
#define COPY_MSG					0xF023
#define PASTE_MSG					0xF024
#define CREATE_INPUT_MSG			0xF025
#define OSC_START_MSG				0xF100 // 255 osc curfs max -->
#define OSC_END_MSG					0xF1FF // <--
#define LEARN_PRESETS_START_MSG		0xF200 // 255 fx learn max -->
#define LEARN_PRESETS_END_MSG		0xF2FF // <--
#define SET_TRACK_START_MSG			0xF300 // 255 track max -->
#define SET_TRACK_END_MSG			0xF3FF // <--
#define SET_PRESET_START_MSG		0xF400 // a bunch of presets -->
#define SET_PRESET_END_MSG			0xFFFE // <-- note: exclude very last LOWROD value to prevent compilation warning

#define MAX_CC_DELAY				3000
#define DEF_CC_DELAY				500
#define MAX_FADE					100
#define DEF_FADE					25

#define MAX_PRESET_COUNT			(SET_PRESET_END_MSG - SET_PRESET_START_MSG)
#define UNDO_STR					__LOCALIZE("Live Configs edition", "sws_undo")

#define APPLY_OSC					"/snm/liveconfig%d/current/changed"
#define APPLYING_OSC				"/snm/liveconfig%d/current/changing"
#define PRELOAD_OSC					"/snm/liveconfig%d/preload/changed"
#define PRELOADING_OSC				"/snm/liveconfig%d/preload/changing"

#define ELLIPSIZE					24

#define APPLY_MASK					1
#define PRELOAD_MASK				2


enum {
  BTNID_ENABLE=2000, //JFB would be great to have _APS_NEXT_CONTROL_VALUE *always* defined
  CMBID_CONFIG,
  TXTID_INPUT_TRACK,
  CMBID_INPUT_TRACK,
  BTNID_LEARN,
  BTNID_OPTIONS,
  BTNID_MON,
  WNDID_CC_DELAY,
  KNBID_CC_DELAY,
  WNDID_FADE,
  KNBID_FADE
};

enum {
  COL_CC=0,
  COL_COMMENT,
  COL_TR,
  COL_TRT,
  COL_FXC,
  COL_PRESET,
  COL_ACTION_ON,
  COL_ACTION_OFF,
  COL_COUNT
};


SNM_WindowManager<LiveConfigsWnd> g_lcWndMgr(LIVECFG_WND_ID);
SNM_MultiWindowManager<LiveConfigMonitorWnd> g_monWndsMgr(LIVECFG_MON_WND_ID);

SWSProjConfig<WDL_PtrList_DeleteOnDestroy<LiveConfig> > g_liveConfigs;
WDL_PtrList<LiveConfigItem> g_clipboardConfigs; // for cut/copy/paste
int g_configId = 0; // the current *displayed/edited* config id

// prefs
char g_lcBigFontName[64] = SNM_DYN_FONT_NAME;


///////////////////////////////////////////////////////////////////////////////
// Presets helpers
///////////////////////////////////////////////////////////////////////////////

// updates a preset conf string
// _presetConf: in/out param
// _fx: 1-based
// _preset: new preset name or NULL or "" to remove
void UpdatePresetConf(WDL_FastString* _presetConf, int _fx, const char* _preset)
{
	WDL_FastString newConf, escStr, escPresetName;

	if (_preset)
		makeEscapedConfigString(_preset, &escPresetName);

	LineParser lp(false);
	if (_presetConf && !lp.parse(_presetConf->Get()))
	{
		bool found = false;
		char fxBuf[32]="";
		if (_snprintfStrict(fxBuf, sizeof(fxBuf), "FX%d:", _fx) > 0)
		{
			for (int i=0; i < lp.getnumtokens(); i++)
			{
				if (!strcmp(lp.gettoken_str(i), fxBuf))
				{
					// set the new preset, if not removed
					if (_preset && *_preset)
					{
						if ((i+1) < lp.getnumtokens())
						{
							found = true;
							if (newConf.GetLength())
								newConf.Append(" ");
							newConf.Append(lp.gettoken_str(i));
							newConf.Append(" ");
							newConf.Append(escPresetName.Get());
						}
					}
				}
				else if (!strncmp(lp.gettoken_str(i), "FX", 2))
				{
					if ((i+1) < lp.getnumtokens())
					{
						if (newConf.GetLength())
							newConf.Append(" ");
						newConf.Append(lp.gettoken_str(i));
						newConf.Append(" ");
						makeEscapedConfigString(lp.gettoken_str(i+1), &escStr);
						newConf.Append(escStr.Get());
					}
				}
				i++; // skip next token
			}
		}

		if (!found && _preset && *_preset) {
			if (newConf.GetLength())
				newConf.Append(" ");
			newConf.AppendFormatted(256, "FX%d: ", _fx);
			newConf.Append(escPresetName.Get());
		}
	}
	if (_presetConf)
		_presetConf->Set(&newConf);
}

bool GetPresetConf(int _fx, const char* _presetConf, WDL_FastString* _presetName)
{
	if (_presetName)
		_presetName->Set("");

	LineParser lp(false);
	if (_presetConf && _presetName && !lp.parse(_presetConf))
	{
		char fxBuf[32]="";
		if (_snprintfStrict(fxBuf, sizeof(fxBuf), "FX%d:", _fx+1) > 0)
		{
			for (int i=0; i < lp.getnumtokens(); i++)
			{
				if (!strcmp(lp.gettoken_str(i), fxBuf))
				{
					if (i+1 < lp.getnumtokens()) {
						_presetName->Set(lp.gettoken_str(i+1));
						return true;
					}
					break;
				}
			}
		}
	}
	return false;
}

int GetPresetFromConfTokenV1(const char* _preset) {
	if (const char* p = strchr(_preset, '.'))
		return atoi(p+1);
	return 0;
}

void UpgradePresetConfV1toV2(MediaTrack* _tr, const char* _confV1, WDL_FastString* _confV2)
{
	if (_tr && _confV1 && *_confV1 && _confV2 && TrackFX_GetCount(_tr))
	{
		_confV2->Set("");

#ifdef _WIN32 // fx presets were not available on OSX before v2
		LineParser lp(false);
		if (!lp.parse(_confV1))
		{
			WDL_FastString escStr;
			for (int i=0; i < lp.getnumtokens(); i++)
			{
				if (_confV2->GetLength())
					_confV2->Append(" ");

				int success, fx = (int)floor(lp.gettoken_float(i, &success)) - 1; // float because token is like "1.3"
				int preset = success ? GetPresetFromConfTokenV1(lp.gettoken_str(i)) - 1 : -1;

				WDL_PtrList_DeleteOnDestroy<WDL_FastString> names;
				int presetCount = preset>=0 ? GetUserPresetNames(_tr, fx, &names) : 0;
				if (presetCount && preset>=0 && preset<presetCount) {
					makeEscapedConfigString(names.Get(preset)->Get(), &escStr);
					_confV2->AppendFormatted(256, "FX%d: %s", fx+1, escStr.Get());
				}
			}
		}
#endif
	}
}

// trigger several track fx presets
// _presetConf: "fx.preset", both 1-based - e.g. "2.9 3.2" => FX2 preset 9, FX3 preset 2 
bool TriggerFXPresets(MediaTrack* _tr, WDL_FastString* _presetConf)
{
	bool updated = false;
	if (int nbFx = ((_tr && _presetConf && _presetConf->GetLength()) ? TrackFX_GetCount(_tr) : 0))
	{
		WDL_FastString presetName;
		for (int i=0; i < nbFx; i++)
			if (GetPresetConf(i, _presetConf->Get(), &presetName))
				updated |= TrackFX_SetPreset(_tr, i, presetName.Get());
	}
	return updated;
}


///////////////////////////////////////////////////////////////////////////////
// LiveConfigItem
///////////////////////////////////////////////////////////////////////////////

LiveConfigItem::LiveConfigItem(int _cc, const char* _desc, MediaTrack* _track, 
	const char* _trTemplate, const char* _fxChain, const char* _presets, 
	const char* _onAction, const char* _offAction)
	: m_cc(_cc),m_desc(_desc),m_track(_track),
	m_trTemplate(_trTemplate),m_fxChain(_fxChain),m_presets(_presets),
	m_onAction(_onAction),m_offAction(_offAction) 
{
}

LiveConfigItem::LiveConfigItem(LiveConfigItem* _item)
{
	Paste(_item);
	m_cc = _item ? _item->m_cc : -1;
}

void LiveConfigItem::Paste(LiveConfigItem* _item)
{
	if (_item)
	{
		// paste everything except the cc value
//		m_cc = _item->m_cc;
		m_desc.Set(_item->m_desc.Get());
		m_track = _item->m_track;
		m_trTemplate.Set(_item->m_trTemplate.Get());
		m_fxChain.Set(_item->m_fxChain.Get());
		m_presets.Set(_item->m_presets.Get());
		m_onAction.Set(_item->m_onAction.Get());
		m_offAction.Set(_item->m_offAction.Get());
	}
	else
		Clear();
}

bool LiveConfigItem::IsDefault(bool _ignoreComment) 
{ 
	return (
		!m_track &&
		(_ignoreComment || (!_ignoreComment && !m_desc.GetLength())) &&
		!m_trTemplate.GetLength() &&
		!m_fxChain.GetLength() &&
		!m_presets.GetLength() && 
		!m_onAction.GetLength() &&
		!m_offAction.GetLength());
}

void LiveConfigItem::Clear(bool _trDataOnly)
{
	m_track=NULL; 
	m_trTemplate.Set("");
	m_fxChain.Set("");
	m_presets.Set("");
	if (!_trDataOnly)
	{
		m_desc.Set("");
		m_onAction.Set("");
		m_offAction.Set("");
	}
}

bool LiveConfigItem::Equals(LiveConfigItem* _item, bool _ignoreComment)
{
	return (_item &&
		m_track == _item->m_track &&
		(_ignoreComment || (!_ignoreComment && !strcmp(m_desc.Get(), _item->m_desc.Get()))) &&
		!strcmp(m_trTemplate.Get(), _item->m_trTemplate.Get()) &&
		!strcmp(m_fxChain.Get(), _item->m_fxChain.Get()) &&
		!strcmp(m_presets.Get(), _item->m_presets.Get()) &&
		!strcmp(m_onAction.Get(), _item->m_onAction.Get()) &&
		!strcmp(m_offAction.Get(), _item->m_offAction.Get()));
}

void LiveConfigItem::GetInfo(WDL_FastString* _info)
{
	if (!_info) return;

	_info->Set("");
	if (!IsDefault(false)) // false: "comments-only" configs can be useful (osc feedback)
	{
		if (m_desc.GetLength())
		{
			_info->Set(m_desc.Get());
			_info->Ellipsize(ELLIPSIZE, ELLIPSIZE);
		}
		else
		{
			if (m_track)
			{
				char* name = (char*)GetSetMediaTrackInfo(m_track, "P_NAME", NULL);
				if (name && *name) _info->Set(name);
				else _info->SetFormatted(32, "[%d]", CSurf_TrackToID(m_track, false));
			}
			_info->Ellipsize(ELLIPSIZE, ELLIPSIZE);

			WDL_FastString line2;
			if (m_desc.GetLength()) line2.Append(m_desc.Get());
			else if (m_trTemplate.GetLength()) line2.Append(m_trTemplate.Get());
			else if (m_fxChain.GetLength()) line2.Append(m_fxChain.Get());
			else if (m_presets.GetLength()) line2.Append(m_presets.Get());
			else if (m_onAction.GetLength()) line2.Append(kbd_getTextFromCmd(NamedCommandLookup(m_onAction.Get()), NULL));
			else if (m_offAction.GetLength()) line2.Append(kbd_getTextFromCmd(NamedCommandLookup(m_offAction.Get()), NULL));
			line2.Ellipsize(ELLIPSIZE, ELLIPSIZE);

			if (_info->GetLength()) _info->Append("\n");
			_info->Append(line2.Get());
		}
	}
	else
		_info->Set(__LOCALIZE("<EMPTY>","sws_DLG_169")); // sws_DLG_169 = monitoring wnd
}


///////////////////////////////////////////////////////////////////////////////
// LiveConfig
///////////////////////////////////////////////////////////////////////////////

LiveConfig::LiveConfig()
{ 
	m_version = LIVECFG_VERSION;
	m_ccDelay = DEF_CC_DELAY;
	m_fade = DEF_FADE;
	m_enable = m_cc123 = m_autoSends = m_selScroll = 1;
	m_ignoreEmpty = m_muteOthers = m_offlineOthers = 0;
	memcpy(&m_inputTr, &GUID_NULL, sizeof(GUID));
	m_activeMidiVal = m_preloadMidiVal = m_curMidiVal = m_curPreloadMidiVal = -1;
	m_osc = NULL;
	for (int j=0; j<SNM_LIVECFG_NB_ROWS; j++)
		m_ccConfs.Add(new LiveConfigItem(j, "", NULL, "", "", "", "", ""));
}

bool LiveConfig::IsDefault(bool _ignoreComment)
{
	LiveConfig temp; // trick: no code update when changing default values
	bool isDefault = 
		m_version==temp.m_version && m_ccDelay==temp.m_ccDelay &&
		m_fade==temp.m_fade && m_enable==temp.m_enable && 
		m_cc123==temp.m_cc123 && m_autoSends==temp.m_autoSends &&
		m_selScroll==temp.m_selScroll && m_ignoreEmpty==temp.m_ignoreEmpty &&
		m_muteOthers==temp.m_muteOthers && m_offlineOthers==temp.m_offlineOthers &&
		GuidsEqual(&m_inputTr, temp.GetInputTrackGUID()) &&
/* those ones can be ignored (for undo only)
		m_activeMidiVal==temp.m_activeMidiVal && m_preloadMidiVal==temp.m_preloadMidiVal &&
		m_curMidiVal==temp.m_curMidiVal && m_curPreloadMidiVal==temp.m_curPreloadMidiVal &&
*/
		m_osc==temp.m_osc;

	for (int j=0; isDefault && j<m_ccConfs.GetSize(); j++)
		if (LiveConfigItem* item = m_ccConfs.Get(j))
			isDefault &= item->IsDefault(_ignoreComment);

	return isDefault;
}

// primitive (no undo point, no ui refresh)
int LiveConfig::SetInputTrack(MediaTrack* _newInputTr, bool _updateSends)
{
	int nbSends = 0;
	MediaTrack* inputTr = GetInputTrack();
	if (_updateSends && inputTr != _newInputTr)
	{
		WDL_PtrList<void> doneTrs;

		// list used to factorize chunk state updates, auto-destroy => auto-commit when exiting!
		WDL_PtrList_DeleteOnDestroy<SNM_ChunkParserPatcher> ps;

		// remove sends of the current input track
		if (inputTr && CSurf_TrackToID(inputTr, false) > 0)
		{
			for (int i=0; i < m_ccConfs.GetSize(); i++)
				if (LiveConfigItem* cfg = m_ccConfs.Get(i))
					if (cfg->m_track && cfg->m_track != _newInputTr && doneTrs.Find(cfg->m_track)<0)
					{
						doneTrs.Add(cfg->m_track);
						SNM_SendPatcher* p = (SNM_SendPatcher*)SNM_FindCPPbyObject(&ps, cfg->m_track);
						if (!p) {
							p = new SNM_SendPatcher(cfg->m_track); 
							ps.Add(p);
						}
						p->RemoveReceivesFrom(inputTr);
					}
		}

		// add sends to the new input track
		if (_newInputTr && CSurf_TrackToID(_newInputTr, false) > 0)
		{
			doneTrs.Empty();
			for (int i=0; i < m_ccConfs.GetSize(); i++)
				if (LiveConfigItem* cfg = m_ccConfs.Get(i))
					if (cfg->m_track &&
						cfg->m_track != _newInputTr && 
						cfg->m_track != inputTr && // exclude the previous input track 
						doneTrs.Find(cfg->m_track)<0 && // have to check this (HasReceives() is not aware of chunk updates that are occuring)
						!HasReceives(_newInputTr, cfg->m_track))
					{
						doneTrs.Add(cfg->m_track);
						SNM_SendPatcher* p = (SNM_SendPatcher*)SNM_FindCPPbyObject(&ps, cfg->m_track);
						if (!p) {
							p = new SNM_SendPatcher(cfg->m_track); 
							ps.Add(p);
						}
						char vol[32] = "1.00000000000000";
						char pan[32] = "0.00000000000000";
						_snprintfSafe(vol, sizeof(vol), "%.14f", *(double*)GetConfigVar("defsendvol"));
						if (p->AddReceive(_newInputTr, *(int*)GetConfigVar("defsendflag")&0xFF, vol, pan))
							nbSends++;
					}
		}
		RefreshRoutingsUI();
	}
	memcpy(&m_inputTr, _newInputTr ? GetTrackGUID(_newInputTr) : &GUID_NULL, sizeof(GUID));
	return nbSends;
}

bool LiveConfig::IsLastConfiguredTrack(MediaTrack* _tr)
{
	int cnt = 0;
	if (_tr)
		for (int i=0; i < m_ccConfs.GetSize(); i++)
			if (LiveConfigItem* cfg = m_ccConfs.Get(i))
				if (cfg->m_track == _tr)
					if (++cnt>1)
						return false;
	return (cnt==1);
}


///////////////////////////////////////////////////////////////////////////////
// LiveConfigView
///////////////////////////////////////////////////////////////////////////////

// !WANT_LOCALIZE_STRINGS_BEGIN:sws_DLG_155
static SWS_LVColumn s_liveCfgListCols[] = { 
	{95,2,"Controller value"}, {150,1,"Comment"}, {150,2,"Track"}, {175,2,"Track template"}, {175,2,"FX Chain"}, {150,2,"FX presets"}, {150,1,"Activate action"}, {150,1,"Deactivate action"}};
// !WANT_LOCALIZE_STRINGS_END

LiveConfigView::LiveConfigView(HWND hwndList, HWND hwndEdit)
:SWS_ListView(hwndList, hwndEdit, COL_COUNT, s_liveCfgListCols, "LiveConfigsViewState", true, "sws_DLG_155")
{
}

void LiveConfigView::GetItemText(SWS_ListItem* item, int iCol, char* str, int iStrMax)
{
	if (str) *str = '\0';
	if (LiveConfigItem* pItem = (LiveConfigItem*)item)
	{
		switch (iCol)
		{
			case COL_CC:
			{
				LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId);
				_snprintfSafe(str, iStrMax, "%03d %s", pItem->m_cc, 
					lc ? (pItem->m_cc==lc->m_activeMidiVal ? UTF8_BULLET : 
						(pItem->m_cc == lc->m_preloadMidiVal ? UTF8_CIRCLE : " ")) : " ");
				break;
			}
			case COL_COMMENT:
				lstrcpyn(str, pItem->m_desc.Get(), iStrMax);
				break;
			case COL_TR:
				if (pItem->m_track && CSurf_TrackToID(pItem->m_track, false) > 0)
					if (char* name = (char*)GetSetMediaTrackInfo(pItem->m_track, "P_NAME", NULL))
						_snprintfSafe(str, iStrMax, "[%d] \"%s\"", CSurf_TrackToID(pItem->m_track, false), name);
				break;
			case COL_TRT:
				GetFilenameNoExt(pItem->m_trTemplate.Get(), str, iStrMax);
				break;
			case COL_FXC:
				GetFilenameNoExt(pItem->m_fxChain.Get(), str, iStrMax);
				break;
			case COL_PRESET:
				lstrcpyn(str, pItem->m_presets.Get(), iStrMax);
				break;
			case COL_ACTION_ON:
			case COL_ACTION_OFF:
			{
				const char* custId = (iCol==COL_ACTION_ON ? pItem->m_onAction.Get() : pItem->m_offAction.Get());
				if (*custId)
				{
					// do not use m_iEditingItem here: it's set to -1 after the actual update, contrary to m_iEditingCol
					// => display IDs when editing a cell, action names otherwise
					if (int cmd = SNM_NamedCommandLookup(custId, NULL)) {
//						lstrcpyn(str, m_iEditingCol<0 && cmd>0 ? kbd_getTextFromCmd(cmd, NULL) : custId, iStrMax);
						lstrcpyn(str, m_iEditingCol<0 && cmd>0 ? SNM_GetTextFromCmd(cmd, NULL) : custId, iStrMax);
					}
				}
				break;
			}
		}
	}
}

void LiveConfigView::SetItemText(SWS_ListItem* _item, int _iCol, const char* _str)
{
	if (LiveConfigItem* pItem = (LiveConfigItem*)_item)
	{
		switch (_iCol)
		{
			case COL_COMMENT:
				// limit the comment size (for RPP files)
				pItem->m_desc.Set(_str);
				pItem->m_desc.Ellipsize(32,32);
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
				break;
			case COL_ACTION_ON:
			case COL_ACTION_OFF:
				if (!*_str || !NamedCommandLookup(_str))
				{
					WDL_FastString msg;
					msg.SetFormatted(256, __LOCALIZE_VERFMT("Unknown command ID or identifier string: '%s'","sws_DLG_155"), _str);
					MessageBox(GetParent(m_hwndList), msg.Get(), __LOCALIZE("S&M - Error","sws_DLG_155"), MB_OK);
					return;
				}
				if (_iCol == COL_ACTION_ON)
					pItem->m_onAction.Set(_str);
				else
					pItem->m_offAction.Set(_str);
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
				break;
		}
	}
}

void LiveConfigView::GetItemList(SWS_ListItemList* pList)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId))
		if (WDL_PtrList<LiveConfigItem>* ccConfs = &lc->m_ccConfs)
			for (int i=0; i < ccConfs->GetSize(); i++)
				pList->Add((SWS_ListItem*)ccConfs->Get(i));
}

// gotcha! several calls for one selection change (eg. 3 unselected rows + 1 new selection)
void LiveConfigView::OnItemSelChanged(SWS_ListItem* item, int iState)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId);
	if (lc && lc->m_selScroll)
	{
		LiveConfigItem* pItem = (LiveConfigItem*)item;
		if (pItem && pItem->m_track && CSurf_TrackToID(pItem->m_track, false) > 0)
		{
			if ((iState & LVIS_SELECTED ? true : false) != (*(int*)GetSetMediaTrackInfo(pItem->m_track, "I_SELECTED", NULL) ? true : false))
				GetSetMediaTrackInfo(pItem->m_track, "I_SELECTED", iState & LVIS_SELECTED ? &g_i1 : &g_i0);
			if (iState & LVIS_SELECTED)
				ScrollTrack(pItem->m_track, true, true);
		}
	}
}

void LiveConfigView::OnItemDblClk(SWS_ListItem* item, int iCol)
{
	if (LiveConfigItem* pItem = (LiveConfigItem*)item)
	{
		if (LiveConfigsWnd* w = g_lcWndMgr.Get())
		{
			switch (iCol)
			{
				case COL_TRT:
					if (pItem->m_track)
						w->OnCommand(LOAD_TRACK_TEMPLATE_MSG, 0);
					break;
				case COL_FXC:
					if (pItem->m_track)
						w->OnCommand(LOAD_FXCHAIN_MSG, 0);
					break;
				default:
					if (w)
						w->OnCommand(APPLY_MSG, 0);
					break;
			}
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// LiveConfigsWnd
///////////////////////////////////////////////////////////////////////////////

// S&M windows lazy init: below's "" prevents registering the SWS' screenset callback
// (use the S&M one instead - already registered via SNM_WindowManager::Init())
LiveConfigsWnd::LiveConfigsWnd()
	: SWS_DockWnd(IDD_SNM_LIVE_CONFIGS, __LOCALIZE("Live Configs","sws_DLG_155"), "", SWSGetCommandID(OpenLiveConfig))
{
	m_id.Set(LIVECFG_WND_ID);
	// Must call SWS_DockWnd::Init() to restore parameters and open the window if necessary
	Init();
}

LiveConfigsWnd::~LiveConfigsWnd()
{
	m_vwndCC.RemoveAllChildren(false);
	m_vwndCC.SetRealParent(NULL);
	m_vwndFade.RemoveAllChildren(false);
	m_vwndFade.SetRealParent(NULL);
}

void LiveConfigsWnd::OnInitDlg()
{
	m_resize.init_item(IDC_LIST, 0.0, 0.0, 1.0, 1.0);
	m_pLists.Add(new LiveConfigView(GetDlgItem(m_hwnd, IDC_LIST), GetDlgItem(m_hwnd, IDC_EDIT)));


	LICE_CachedFont* font = SNM_GetThemeFont();

	m_vwnd_painter.SetGSC(WDL_STYLE_GetSysColor);
	m_parentVwnd.SetRealParent(m_hwnd);

	m_btnEnable.SetID(BTNID_ENABLE);
	m_btnEnable.SetFont(font);
	m_btnEnable.SetTextLabel(__LOCALIZE("Config #","sws_DLG_155"));
	m_parentVwnd.AddChild(&m_btnEnable);

	m_cbConfig.SetID(CMBID_CONFIG);
	m_cbConfig.SetFont(font);
	char cfg[4] = "";
	for (int i=0; i<SNM_LIVECFG_NB_CONFIGS; i++) {
		_snprintfSafe(cfg, sizeof(cfg), "%d", i+1);
		m_cbConfig.AddItem(cfg);
	}
	m_cbConfig.SetCurSel(g_configId);
	m_parentVwnd.AddChild(&m_cbConfig);

	m_txtInputTr.SetID(TXTID_INPUT_TRACK);
	m_txtInputTr.SetFont(font);
	m_txtInputTr.SetText(__LOCALIZE("Input track:","sws_DLG_155"));
	m_parentVwnd.AddChild(&m_txtInputTr);

	m_cbInputTr.SetID(CMBID_INPUT_TRACK);
	m_cbInputTr.SetFont(font);
	m_parentVwnd.AddChild(&m_cbInputTr);

	m_btnLearn.SetID(BTNID_LEARN);
	m_parentVwnd.AddChild(&m_btnLearn);

	m_btnOptions.SetID(BTNID_OPTIONS);
	m_parentVwnd.AddChild(&m_btnOptions);

	m_btnMonitor.SetID(BTNID_MON);
	m_parentVwnd.AddChild(&m_btnMonitor);

	m_knobCC.SetID(KNBID_CC_DELAY);
	m_knobCC.SetRange(0, MAX_CC_DELAY, DEF_CC_DELAY);
	m_vwndCC.AddChild(&m_knobCC);

	m_vwndCC.SetID(WNDID_CC_DELAY);
	m_vwndCC.SetTitle(__LOCALIZE("Smoothing:","sws_DLG_155"));
	m_vwndCC.SetSuffix(__LOCALIZE("ms","sws_DLG_155"));
	m_vwndCC.SetZeroText(__LOCALIZE("immediate","sws_DLG_155"));
	m_parentVwnd.AddChild(&m_vwndCC);

	m_knobFade.SetID(KNBID_FADE);
	m_knobFade.SetRangeFactor(0, MAX_FADE, DEF_FADE, 10.0);
	m_vwndFade.AddChild(&m_knobFade);

	m_vwndFade.SetID(WNDID_FADE);
	m_vwndFade.SetTitle(__LOCALIZE("Tiny fades:","sws_DLG_155"));
	m_vwndFade.SetSuffix(__LOCALIZE("ms","sws_DLG_155"));
	m_vwndFade.SetZeroText(__LOCALIZE("disabled","sws_DLG_155"));
	m_parentVwnd.AddChild(&m_vwndFade);

	Update();
}

void LiveConfigsWnd::OnDestroy() 
{
	m_cbConfig.Empty();
	m_cbInputTr.Empty();
	m_vwndCC.RemoveAllChildren(false);
	m_vwndCC.SetRealParent(NULL);
	m_vwndFade.RemoveAllChildren(false);
	m_vwndFade.SetRealParent(NULL);
}

void LiveConfigsWnd::FillComboInputTrack()
{
	m_cbInputTr.Empty();
	m_cbInputTr.AddItem(__LOCALIZE("None","sws_DLG_155"));
	for (int i=1; i <= GetNumTracks(); i++) // excl. master
	{
		WDL_FastString ellips;
		char* name = (char*)GetSetMediaTrackInfo(CSurf_TrackFromID(i,false), "P_NAME", NULL);
		ellips.SetFormatted(SNM_MAX_TRACK_NAME_LEN, "[%d] \"%s\"", i, name?name:"");
		ellips.Ellipsize(ELLIPSIZE, ELLIPSIZE);
		m_cbInputTr.AddItem(ellips.Get());
	}

	int sel=0;
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId))
		if (MediaTrack* inputTr = lc->GetInputTrack())
			for (int i=1; i <= GetNumTracks(); i++) // excl. master
				if (inputTr == CSurf_TrackFromID(i, false)) {
					sel = i;
					break;
				}
	m_cbInputTr.SetCurSel(sel);
}

// "by cc" in order to support sort
bool LiveConfigsWnd::SelectByCCValue(int _configId, int _cc, bool _selectOnly)
{
	if (_configId == g_configId)
	{
		SWS_ListView* lv = GetListView();
		HWND hList = lv ? lv->GetHWND() : NULL;
		if (lv && hList) // this can be called when the view is closed!
		{
			for (int i=0; i < lv->GetListItemCount(); i++)
			{
				LiveConfigItem* item = (LiveConfigItem*)lv->GetListItem(i);
				if (item && item->m_cc == _cc) 
				{
					if (_selectOnly)
						ListView_SetItemState(hList, -1, 0, LVIS_SELECTED);
					ListView_SetItemState(hList, i, LVIS_SELECTED, LVIS_SELECTED); 
					ListView_EnsureVisible(hList, i, true);
//					Update();
					return true;
				}
			}
		}
	}
	return false;
}

void LiveConfigsWnd::Update()
{
	FillComboInputTrack();

	if (m_pLists.GetSize())
		GetListView()->Update();

	if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId)) {
		m_vwndCC.SetValue(lc->m_ccDelay);
		m_vwndFade.SetValue(lc->m_fade);
	}
	m_parentVwnd.RequestRedraw(NULL);
}

void LiveConfigsWnd::OnCommand(WPARAM wParam, LPARAM lParam)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId);
	if (!lc || !m_pLists.GetSize())
		return;

	int x=0;
	MediaTrack* inputTr = lc->GetInputTrack();
	LiveConfigItem* item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
	switch (LOWORD(wParam))
	{
		case HELP_MSG:
			ShellExecute(m_hwnd, "open", "http://www.sws-extension.org/download/S&M_LiveConfigs.pdf" , NULL, NULL, SW_SHOWNORMAL); //JFB! oudated
			break;
		case CREATE_INPUT_MSG:
		{
			char trName[SNM_MAX_TRACK_NAME_LEN] = "";
			if (PromptUserForString(GetMainHwnd(), __LOCALIZE("S&M - Create input track","sws_DLG_155"), trName, sizeof(trName), true))
			{
				// no Undo_OnStateChangeEx2() here, rec arm below creates an undo point
				// (fixed in REAPER v4.5 but kept for older versions..)
				Undo_BeginBlock2(NULL); 

				InsertTrackAtIndex(GetNumTracks(), false);
				TrackList_AdjustWindows(false);

				inputTr = CSurf_TrackFromID(GetNumTracks(), false);
				if (inputTr)
				{
					GetSetMediaTrackInfo(inputTr, "P_NAME", trName);
					GetSetMediaTrackInfo(inputTr, "B_MAINSEND", &g_bFalse);
					GetSetMediaTrackInfo(inputTr, "I_RECMON", &g_i1);
					GetSetMediaTrackInfo(inputTr, "I_RECARM", &g_i1);
					SNM_SetSelectedTrack(NULL, inputTr, true);
					ScrollSelTrack(true, true);
				}

				int nbSends = lc->SetInputTrack(inputTr, true); // always obey (ignore lc->m_autoSends)
				Undo_EndBlock2(NULL, UNDO_STR, UNDO_STATE_ALL);

				Update();

				char msg[256] = "";
				if (nbSends > 0) 
					_snprintfSafe(msg, sizeof(msg), __LOCALIZE_VERFMT("Created track \"%s\" (%d sends, record armed, monitoring enabled, no master sends).\nPlease select a MIDI or audio input for this new track.","sws_DLG_155"), trName, nbSends);
				else
					_snprintfSafe(msg, sizeof(msg), __LOCALIZE_VERFMT("Created track \"%s\" (record armed, monitoring enabled, no master sends).\nPlease select a MIDI or audio input for this new track.","sws_DLG_155"), trName);
				MessageBox(GetHWND(), msg, __LOCALIZE("S&M - Create input track","sws_DLG_155"), MB_OK);
			}
			break;
		}
		case SHOW_FX_MSG:
			if (item && item->m_track)
				TrackFX_Show(item->m_track, 0, 1);
			break;
		case SHOW_IO_MSG:
			if (item && item->m_track)
				ShowTrackRoutingWindow(item->m_track);
			break;
		case SHOW_FX_INPUT_MSG:
			if (inputTr)
				TrackFX_Show(inputTr, 0, 1);
			break;
		case SHOW_IO_INPUT_MSG:
			if (inputTr)
				ShowTrackRoutingWindow(inputTr);
			break;
		
		case MUTE_OTHERS_MSG:
			if (!lc->m_muteOthers) // about to activate?
				if (int* dontProcessMutedTrs = (int*)GetConfigVar("norunmute"))
					if (!*dontProcessMutedTrs)
					{
						// do not enable in the user's back:
						int r = MessageBox(GetHWND(),
							__LOCALIZE("The preference \"Do not process muted tracks\" is disabled.\nDo you want to enable it for CPU savings?","sws_DLG_155"),
							__LOCALIZE("S&M - Question","sws_DLG_155"),
							MB_YESNOCANCEL);
						if (r==IDYES)
							*dontProcessMutedTrs = 1;
						else if (r==IDCANCEL)
							break;
					}
			lc->m_muteOthers = !lc->m_muteOthers;
			Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			break;
		case SELSCROLL_MSG:
			lc->m_selScroll = !lc->m_selScroll;
			Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			break;
		case OFFLINE_OTHERS_MSG:
			if (!lc->m_offlineOthers || lc->m_offlineOthers == 2) lc->m_offlineOthers = 1;
			else lc->m_offlineOthers = 0;
			Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			break;
		case CC123_MSG:
			lc->m_cc123 = !lc->m_cc123;
			Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			break;
		case IGNORE_EMPTY_MSG:
			lc->m_ignoreEmpty = !lc->m_ignoreEmpty;
			Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			break;
		case AUTOSENDS_MSG:
			lc->m_autoSends = !lc->m_autoSends;
			Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			break;

		case INS_UP_MSG:
			Insert(-1);
			break;
		case INS_DOWN_MSG:
			Insert(1);
			break;
		case PASTE_MSG:
			if (item)
			{
				int sortCol = GetListView()->GetSortColumn();
				if (sortCol && sortCol!=1)
				{
					//JFB lazy here.. better than confusing the user..
					MessageBox(GetHWND(), __LOCALIZE("The list view must be sorted by ascending values!","sws_DLG_155"), __LOCALIZE("S&M - Error","sws_DLG_155"), MB_OK);
					break;
				}
				bool updt = false, firstSel = true;
				int destIdx = lc->m_ccConfs.Find(item);
				if (destIdx>=0)
				{
					for (int i=0; i<g_clipboardConfigs.GetSize() && (destIdx+i)<lc->m_ccConfs.GetSize(); i++)
						if ((item = lc->m_ccConfs.Get(destIdx+i)))
							if (LiveConfigItem* pasteItem = g_clipboardConfigs.Get(i))
							{
								item->Paste(pasteItem); // copy everything except the cc value
								updt = true;
								if (SelectByCCValue(g_configId, item->m_cc, firstSel)) firstSel = false;
							}
				}
				if (updt)
				{
					Update(); // preserve list view selection
					Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_ALL, -1); // UNDO_STATE_ALL: possible routing updates above
				}
			}
			break;
		case CUT_MSG:
		case COPY_MSG:
			if (item)
			{
				g_clipboardConfigs.Empty(true);
				LiveConfigItem* item2 = item; // let "item" as it is: code fall through below..
				int x2 = x;
				while (item2) {
					g_clipboardConfigs.Add(new LiveConfigItem(item2));
					item2 = (LiveConfigItem*)GetListView()->EnumSelected(&x2);
				}
				if (LOWORD(wParam) != CUT_MSG) // cut => fall through
					break;
			}
			else
				break;
		case CLEAR_CC_ROW_MSG:
		{
			bool updt = false;
			while (item)
			{
				if (lc->m_autoSends && item->m_track && inputTr && lc->IsLastConfiguredTrack(item->m_track))
					SNM_RemoveReceivesFrom(item->m_track, inputTr);
				item->Clear();
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_ALL, -1); // UNDO_STATE_ALL: possible routing updates above
			}
			break;
		}
		case EDIT_DESC_MSG:
			if (item) GetListView()->EditListItem((SWS_ListItem*)item, COL_COMMENT);
			break;
		case CLEAR_DESC_MSG:
		{
			bool updt = false;
			while (item) {
				item->m_desc.Set("");
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}

		case APPLY_MSG:
			if (item)
				ApplyLiveConfig(g_configId, item->m_cc, true); // immediate
			break;
		case PRELOAD_MSG:
			if (item)
				PreloadLiveConfig(g_configId, item->m_cc, true); // immediate
			break;

		case LOAD_TRACK_TEMPLATE_MSG:
		{
			bool updt = false;
			if (item)
			{
				char fn[SNM_MAX_PATH]="";
				if (BrowseResourcePath(__LOCALIZE("S&M - Load track template","sws_DLG_155"), "TrackTemplates", "REAPER Track Template (*.RTrackTemplate)\0*.RTrackTemplate\0", fn, sizeof(fn)))
				{
					while(item) {
						item->m_trTemplate.Set(fn);
						item->m_fxChain.Set("");
						item->m_presets.Set("");
						updt = true;
						item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
					}
				}
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}
		case CLEAR_TRACK_TEMPLATE_MSG:
		{
			bool updt = false;
			while (item) {
				item->m_trTemplate.Set("");
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}
		case LOAD_FXCHAIN_MSG:
		{
			bool updt = false;
			if (item)
			{
				char fn[SNM_MAX_PATH]="";
				if (BrowseResourcePath(__LOCALIZE("S&M - Load FX Chain","sws_DLG_155"), "FXChains", "REAPER FX Chain (*.RfxChain)\0*.RfxChain\0", fn, sizeof(fn)))
				{
					while(item) {
						item->m_fxChain.Set(fn);
						item->m_trTemplate.Set("");
						item->m_presets.Set("");
						updt = true;
						item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
					}
				}
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}
		case CLEAR_FXCHAIN_MSG:
		{
			bool updt = false;
			while (item)
			{
				item->m_fxChain.Set("");
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}
		case EDIT_ON_ACTION_MSG:
		case EDIT_OFF_ACTION_MSG:
			if (item) 
				GetListView()->EditListItem((SWS_ListItem*)item, LOWORD(wParam)==EDIT_ON_ACTION_MSG ? COL_ACTION_ON : COL_ACTION_OFF);
			break;
		case LEARN_ON_ACTION_MSG:
		case LEARN_OFF_ACTION_MSG:
		{
			char idstr[SNM_MAX_ACTION_CUSTID_LEN] = "";
			if (item && GetSelectedAction(idstr, SNM_MAX_ACTION_CUSTID_LEN))
			{
				bool updt = false;
				while (item)
				{
					if (LOWORD(wParam)==LEARN_ON_ACTION_MSG)
						item->m_onAction.Set(idstr);
					else
						item->m_offAction.Set(idstr);
					updt = true;
					item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
				}
				if (updt) {
					Update();
					Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
				}
			}
			break;
		}
		case CLEAR_ON_ACTION_MSG:
		case CLEAR_OFF_ACTION_MSG:
		{
			bool updt = false;
			while (item)
			{
				if (LOWORD(wParam)==CLEAR_ON_ACTION_MSG)
					item->m_onAction.Set("");
				else
					item->m_offAction.Set("");
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}
		case CLEAR_TRACK_MSG:
		{
			bool updt = false;
			while (item)
			{
				if (lc->m_autoSends && item->m_track && inputTr && lc->IsLastConfiguredTrack(item->m_track))
					SNM_RemoveReceivesFrom(item->m_track, inputTr);
				if (item->m_track) item->Clear(true);
				else item->m_track = NULL;
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_ALL, -1);  // UNDO_STATE_ALL: possible routing updates above
			}
			break;
		}
		case CLEAR_PRESETS_MSG:
		{
			bool updt = false;
			while (item)
			{
				item->m_presets.Set("");
				updt = true;
				item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
			}
			if (updt) {
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			break;
		}
		case BTNID_ENABLE:
			if (!HIWORD(wParam) || HIWORD(wParam)==600)
				UpdateEnableLiveConfig(g_configId, -1);
			break;
		case LEARN_APPLY_MSG:
			LearnAction(SNM_GetMySection(), SNM_SECTION_1ST_CMD_ID + g_configId);
			break;
		case LEARN_PRELOAD_MSG:
			LearnAction(SNM_GetMySection(), SNM_SECTION_1ST_CMD_ID + SNM_LIVECFG_NB_CONFIGS + g_configId);
			break;
		case BTNID_LEARN:
		{
			RECT r; m_btnLearn.GetPositionInTopVWnd(&r);
			ClientToScreen(m_hwnd, (LPPOINT)&r);
			ClientToScreen(m_hwnd, ((LPPOINT)&r)+1);
			SendMessage(m_hwnd, WM_CONTEXTMENU, 0, MAKELPARAM((UINT)(r.left), (UINT)(r.bottom+SNM_1PIXEL_Y)));
			break;
		}
		case BTNID_OPTIONS:
		{
			RECT r; m_btnOptions.GetPositionInTopVWnd(&r);
			ClientToScreen(m_hwnd, (LPPOINT)&r);
			ClientToScreen(m_hwnd, ((LPPOINT)&r)+1);
			SendMessage(m_hwnd, WM_CONTEXTMENU, 0, MAKELPARAM((UINT)(r.left), (UINT)(r.bottom+SNM_1PIXEL_Y)));
			break;
		}
		case BTNID_MON:
			OpenLiveConfigMonitorWnd(g_configId);
			break;
		case CMBID_INPUT_TRACK:
			if (HIWORD(wParam)==CBN_SELCHANGE)
			{
				lc->SetInputTrack(m_cbInputTr.GetCurSel() ? CSurf_TrackFromID(m_cbInputTr.GetCurSel(), false) : NULL, lc->m_autoSends==1);
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_ALL, -1); // UNDO_STATE_ALL: SetInputTrack() might update the project
				Update();
			}
			break;
		case CMBID_CONFIG:
			if (HIWORD(wParam)==CBN_SELCHANGE)
			{
				// stop cell editing (changing the list content would be ignored otherwise => dropdown box & list box unsynchronized)
				GetListView()->EditListItemEnd(false);
				g_configId = m_cbConfig.GetCurSel();
				Update();
			}
			break;

		default:
			if (LOWORD(wParam)>=SET_TRACK_START_MSG && LOWORD(wParam)<=SET_TRACK_END_MSG) 
			{
				bool updt = false;
				while (item)
				{
					item->m_track = CSurf_TrackFromID((int)LOWORD(wParam) - SET_TRACK_START_MSG, false);
					if (!(item->m_track)) {
						item->m_fxChain.Set("");
						item->m_trTemplate.Set("");
					}
					item->m_presets.Set("");
					if (lc->m_autoSends && item->m_track && inputTr && !HasReceives(inputTr, item->m_track))
						SNM_AddReceive(inputTr, item->m_track, -1);
					updt = true;
					item = (LiveConfigItem*)GetListView()->EnumSelected(&x);
				}
				if (updt) {
					Update();
					Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_ALL, -1); // UNDO_STATE_ALL: possible routing updates above
				}
			}
			else if (item && LOWORD(wParam)>=LEARN_PRESETS_START_MSG && LOWORD(wParam)<=LEARN_PRESETS_END_MSG) 
			{
				if (item->m_track)
				{
					int fx = (int)LOWORD(wParam)-LEARN_PRESETS_START_MSG;
					char buf[SNM_MAX_PRESET_NAME_LEN] = "";
					TrackFX_GetPreset(item->m_track, fx, buf, SNM_MAX_PRESET_NAME_LEN);
					UpdatePresetConf(&item->m_presets, fx+1, buf);
					item->m_fxChain.Set("");
					item->m_trTemplate.Set("");
					Update();
					Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
				}
				break;
			}
			else if (item && LOWORD(wParam) >= SET_PRESET_START_MSG && LOWORD(wParam) <= SET_PRESET_END_MSG) 
			{
				if (PresetMsg* msg = m_lastPresetMsg.Get((int)LOWORD(wParam) - SET_PRESET_START_MSG))
				{
					UpdatePresetConf(&item->m_presets, msg->m_fx+1, msg->m_preset.Get());
					item->m_fxChain.Set("");
					item->m_trTemplate.Set("");
					Update();
					Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
				}
			}
			else if (LOWORD(wParam) >= OSC_START_MSG && LOWORD(wParam) <= OSC_END_MSG) 
			{
				DELETE_NULL(lc->m_osc);
				WDL_PtrList_DeleteOnDestroy<SNM_OscCSurf> oscCSurfs;
				LoadOscCSurfs(&oscCSurfs);
				if (SNM_OscCSurf* osc = oscCSurfs.Get((int)LOWORD(wParam)-1 - OSC_START_MSG))
					lc->m_osc = new SNM_OscCSurf(osc);

				UpdateMonitoring(
					g_configId,
					APPLY_MASK|PRELOAD_MASK,
					APPLY_MASK|PRELOAD_MASK,
					2); // osc only
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
			}
			else
				Main_OnCommand((int)wParam, (int)lParam);
			break;
	}
}

INT_PTR LiveConfigsWnd::OnUnhandledMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg==WM_VSCROLL)
		if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId))
			switch (lParam)
			{
				case KNBID_CC_DELAY:
					lc->m_ccDelay = m_knobCC.GetSliderPosition();
					m_vwndCC.SetValue(lc->m_ccDelay);
					ScheduledJob::Schedule(new UndoJob(UNDO_STR, UNDO_STATE_MISCCFG));
					break;
				case KNBID_FADE:
					lc->m_fade = m_knobFade.GetSliderPosition();
					m_vwndFade.SetValue(lc->m_fade);
					ScheduledJob::Schedule(new UndoJob(UNDO_STR, UNDO_STATE_MISCCFG));
					ScheduledJob::Schedule(new LiveConfigsUpdateFadeJob(lc->m_fade));
					break;
			}
	return 0;
}

void LiveConfigsWnd::AddPresetMenu(HMENU _menu, MediaTrack* _tr, WDL_FastString* _curPresetConf)
{
	m_lastPresetMsg.Empty(true);

	int fxCount = (_tr ? TrackFX_GetCount(_tr) : 0);
	if (!fxCount) {
		AddToMenu(_menu, __LOCALIZE("(No FX on track)","sws_DLG_155"), 0, -1, false, MF_GRAYED);
		return;
	}
	
	char fxName[SNM_MAX_FX_NAME_LEN] = "";
	int msgCpt = 0;
	for(int fx=0; fx<fxCount; fx++) 
	{
		if(TrackFX_GetFXName(_tr, fx, fxName, SNM_MAX_FX_NAME_LEN))
		{
			HMENU fxSubMenu = CreatePopupMenu();
			WDL_FastString str, curPresetName;
			GetPresetConf(fx, _curPresetConf->Get(), &curPresetName);

			// learn current preset
			str.Set(__LOCALIZE("[All presets (.fxp, .rpl, etc..)]","sws_DLG_155"));
			AddToMenu(fxSubMenu, str.Get(), 0, -1, false, MF_GRAYED);
			if ((LEARN_PRESETS_START_MSG + fx) <= LEARN_PRESETS_END_MSG)
			{
				char buf[SNM_MAX_PRESET_NAME_LEN] = "";
				TrackFX_GetPreset(_tr, fx, buf, SNM_MAX_PRESET_NAME_LEN);
				str.SetFormatted(256, __LOCALIZE_VERFMT("Learn current preset \"%s\"","sws_DLG_155"), *buf?buf:__LOCALIZE("undefined","sws_DLG_155"));
				AddToMenu(fxSubMenu, str.Get(), LEARN_PRESETS_START_MSG + fx, -1, false, *buf?0:MF_GRAYED);
			}

			if (GetMenuItemCount(fxSubMenu))
				AddToMenu(fxSubMenu, SWS_SEPARATOR, 0);

			// user preset list
			WDL_PtrList_DeleteOnDestroy<WDL_FastString> names;
			int presetCount = GetUserPresetNames(_tr, fx, &names);

			str.SetFormatted(256, __LOCALIZE_VERFMT("[User presets (.rpl)%s]","sws_DLG_155"), presetCount?"":__LOCALIZE(": no .rpl file loaded","sws_DLG_155"));
			AddToMenu(fxSubMenu, str.Get(), 0, -1, false, MF_GRAYED);

			for(int j=0; j<presetCount && (SET_PRESET_START_MSG + msgCpt) <= SET_PRESET_END_MSG; j++)
				if (names.Get(j)->GetLength()) {
					AddToMenu(fxSubMenu, names.Get(j)->Get(), SET_PRESET_START_MSG + msgCpt, -1, false, !strcmp(curPresetName.Get(), names.Get(j)->Get()) ? MFS_CHECKED : MFS_UNCHECKED);
					m_lastPresetMsg.Add(new PresetMsg(fx, names.Get(j)->Get()));
					msgCpt++;
				}

			if (GetMenuItemCount(fxSubMenu))
				AddToMenu(fxSubMenu, SWS_SEPARATOR, 0);

			// clear current fx preset
			if ((SET_PRESET_START_MSG + msgCpt) <= SET_PRESET_END_MSG)
			{
				AddToMenu(fxSubMenu, __LOCALIZE("None","sws_DLG_155"), SET_PRESET_START_MSG + msgCpt, -1, false, !curPresetName.GetLength() ? MFS_CHECKED : MFS_UNCHECKED);
				m_lastPresetMsg.Add(new PresetMsg(fx, ""));
				msgCpt++;
			}

			// add fx sub menu
			str.SetFormatted(512, "FX%d: %s", fx+1, fxName);
			AddSubMenu(_menu, fxSubMenu, str.Get(), -1, GetMenuItemCount(fxSubMenu) ? MFS_ENABLED : MF_GRAYED);
		}
	}
}

void LiveConfigsWnd::AddLearnMenu(HMENU _menu, bool _subItems)
{
	if (g_liveConfigs.Get()->Get(g_configId))
	{
		char buf[128] = "";
		HMENU hOptMenu;
		if (_subItems) hOptMenu = CreatePopupMenu();
		else hOptMenu = _menu;

		_snprintfSafe(buf, sizeof(buf), __LOCALIZE_VERFMT("Apply Live Config %d (MIDI/OSC only)","s&m_section_actions"), g_configId+1);
		AddToMenu(hOptMenu, buf, LEARN_APPLY_MSG);
		_snprintfSafe(buf, sizeof(buf), __LOCALIZE_VERFMT("Preload Live Config %d (MIDI/OSC only)","s&m_section_actions"), g_configId+1);
		AddToMenu(hOptMenu, buf, LEARN_PRELOAD_MSG);
		if (_subItems && GetMenuItemCount(hOptMenu))
			AddSubMenu(_menu, hOptMenu, __LOCALIZE("Learn","sws_DLG_155"));
	}
}

void LiveConfigsWnd::AddOptionsMenu(HMENU _menu, bool _subItems)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId))
	{
		HMENU hOptMenu;
		if (_subItems) hOptMenu = CreatePopupMenu();
		else hOptMenu = _menu;

		AddToMenu(hOptMenu, __LOCALIZE("Mute all but active track (CPU savings)","sws_DLG_155"), MUTE_OTHERS_MSG, -1, false, lc->m_muteOthers ? MFS_CHECKED : MFS_UNCHECKED);
		AddToMenu(hOptMenu, __LOCALIZE("Offline all but active/preloaded tracks (RAM savings)","sws_DLG_155"), OFFLINE_OTHERS_MSG, -1, false, lc->m_offlineOthers ? MFS_CHECKED : MFS_UNCHECKED);
		AddToMenu(hOptMenu, SWS_SEPARATOR, 0);
		AddToMenu(hOptMenu, __LOCALIZE("Ignore switches to empty configs","sws_DLG_155"), IGNORE_EMPTY_MSG, -1, false, lc->m_ignoreEmpty ? MFS_CHECKED : MFS_UNCHECKED);
		AddToMenu(hOptMenu, __LOCALIZE("Send all notes off when switching configs","sws_DLG_155"), CC123_MSG, -1, false, lc->m_cc123 ? MFS_CHECKED : MFS_UNCHECKED);
		AddToMenu(hOptMenu, SWS_SEPARATOR, 0);
		AddToMenu(hOptMenu, __LOCALIZE("Automatically update sends from the input track (if any)","sws_DLG_155"), AUTOSENDS_MSG, -1, false, lc->m_autoSends ? MFS_CHECKED : MFS_UNCHECKED);
		AddToMenu(hOptMenu, __LOCALIZE("Select/scroll to track on list view click","sws_DLG_155"), SELSCROLL_MSG, -1, false, lc->m_selScroll ? MFS_CHECKED : MFS_UNCHECKED);

		if (_subItems && GetMenuItemCount(hOptMenu))
			AddSubMenu(_menu, hOptMenu, __LOCALIZE("Options","sws_DLG_155"));
	}
}

HMENU LiveConfigsWnd::OnContextMenu(int x, int y, bool* wantDefaultItems)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId);
	if (!lc) return NULL;
	MediaTrack* inputTr = lc->GetInputTrack();

	HMENU hMenu = CreatePopupMenu();

	// specific context menu for the option button
	POINT p; GetCursorPos(&p);
	ScreenToClient(m_hwnd, &p);
	if (WDL_VWnd* v = m_parentVwnd.VirtWndFromPoint(p.x,p.y,1))
	{
		switch (v->GetID())
		{
			case BTNID_LEARN:
				*wantDefaultItems = false;
				AddLearnMenu(hMenu, false);
				return hMenu;
			case BTNID_OPTIONS:
				*wantDefaultItems = false;
				AddOptionsMenu(hMenu, false);
				return hMenu;
			case TXTID_INPUT_TRACK:
			case CMBID_INPUT_TRACK:
				*wantDefaultItems = false;
				AddToMenu(hMenu, __LOCALIZE("Create input track...","sws_DLG_155"), CREATE_INPUT_MSG);
				AddToMenu(hMenu, SWS_SEPARATOR, 0);
				AddToMenu(hMenu, __LOCALIZE("Show FX chain...","sws_DLG_155"), SHOW_FX_INPUT_MSG, -1, false, inputTr ? MF_ENABLED : MF_GRAYED);
				AddToMenu(hMenu, __LOCALIZE("Show routing window...","sws_DLG_155"), SHOW_IO_INPUT_MSG, -1, false, inputTr ? MF_ENABLED : MF_GRAYED);
				return hMenu;
		}
	}

	int iCol;
	bool grayed = false;
	if (LiveConfigItem* item = (LiveConfigItem*)GetListView()->GetHitItem(x, y, &iCol))
	{
		*wantDefaultItems = (iCol < 0);
		switch(iCol)
		{
			case COL_COMMENT:
				AddToMenu(hMenu, __LOCALIZE("Edit comment","sws_DLG_155"), EDIT_DESC_MSG);
				AddToMenu(hMenu, __LOCALIZE("Clear comments","sws_DLG_155"), CLEAR_DESC_MSG);
				break;
			case COL_TR:
				{
					int trackCnt = GetNumTracks();
					HMENU hTracksMenu = CreatePopupMenu();
					if (trackCnt)
					{
						char trName[SNM_MAX_TRACK_NAME_LEN] = "";
						for (int i=1; i<=trackCnt; i++)
						{
							char* name = (char*)GetSetMediaTrackInfo(CSurf_TrackFromID(i,false), "P_NAME", NULL);
							_snprintfSafe(trName, sizeof(trName), "[%d] \"%s\"", i, name?name:"");
							AddToMenu(hTracksMenu, trName, SET_TRACK_START_MSG + i);
						}
					}
					else
						AddToMenu(hTracksMenu, __LOCALIZE("[No track found in project!]","sws_DLG_155"), 0, -1, false, MF_GRAYED);
					AddSubMenu(hMenu, hTracksMenu, __LOCALIZE("Set tracks","sws_DLG_155"));
				}
				AddToMenu(hMenu, __LOCALIZE("Clear tracks","sws_DLG_155"), CLEAR_TRACK_MSG);

				if (GetMenuItemCount(hMenu))
					AddToMenu(hMenu, SWS_SEPARATOR, 0);
				AddToMenu(hMenu, __LOCALIZE("Show FX chain...","sws_DLG_155"), SHOW_FX_MSG, -1, false, item->m_track ? MF_ENABLED : MF_GRAYED);
				AddToMenu(hMenu, __LOCALIZE("Show routing window...","sws_DLG_155"), SHOW_IO_MSG, -1, false, item->m_track ? MF_ENABLED : MF_GRAYED);
				break;
			case COL_TRT:
				if (!item->m_track) {
					AddToMenu(hMenu, __LOCALIZE("[Define a track first!]","sws_DLG_155"), 0, -1, false, MF_GRAYED);
					grayed = true;
				}
				else if (item->m_fxChain.GetLength()) {
					AddToMenu(hMenu, __LOCALIZE("[FX Chain overrides!]","sws_DLG_155"), 0, -1, false, MFS_GRAYED);
					grayed = true;
				}
				else if (item->m_presets.GetLength()) {
					AddToMenu(hMenu, __LOCALIZE("[FX preset overrides!]","sws_DLG_155"), 0, -1, false, MFS_GRAYED);
					grayed = true;
				}
				AddToMenu(hMenu, __LOCALIZE("Load track template...","sws_DLG_155"), LOAD_TRACK_TEMPLATE_MSG, -1, false, grayed ? MF_GRAYED : MFS_UNCHECKED);
				AddToMenu(hMenu, __LOCALIZE("Clear track templates","sws_DLG_155"), CLEAR_TRACK_TEMPLATE_MSG, -1, false, grayed ? MF_GRAYED : MFS_UNCHECKED);
				break;
			case COL_FXC:
				if (!item->m_track) {
					AddToMenu(hMenu, __LOCALIZE("[Define a track first!]","sws_DLG_155"), 0, -1, false, MF_GRAYED);
					grayed = true;
				}
				else if (item->m_trTemplate.GetLength()) {
					AddToMenu(hMenu, __LOCALIZE("[Track template overrides!]","sws_DLG_155"), 0, -1, false, MFS_GRAYED);
					grayed = true;
				}
				else if (item->m_presets.GetLength()) {
					AddToMenu(hMenu, __LOCALIZE("[FX preset overrides!]","sws_DLG_155"), 0, -1, false, MFS_GRAYED);
					grayed = true;
				}
				AddToMenu(hMenu, __LOCALIZE("Load FX Chain...","sws_DLG_155"), LOAD_FXCHAIN_MSG, -1, false, grayed ? MF_GRAYED : MFS_UNCHECKED);
				AddToMenu(hMenu, __LOCALIZE("Clear FX Chains","sws_DLG_155"), CLEAR_FXCHAIN_MSG, -1, false, grayed ? MF_GRAYED : MFS_UNCHECKED);
				break;
			case COL_PRESET:
				if (!item->m_track) {
					AddToMenu(hMenu, __LOCALIZE("[Define a track first!]","sws_DLG_155"), 0, -1, false, MF_GRAYED);
					grayed = true;
				}
				else if (item->m_trTemplate.GetLength()) {
					AddToMenu(hMenu, __LOCALIZE("[Track template overrides!]","sws_DLG_155"), 0, -1, false, MFS_GRAYED);
					grayed = true;
				}
				else if (item->m_fxChain.GetLength()) {
					AddToMenu(hMenu, __LOCALIZE("[FX Chain overrides!]","sws_DLG_155"), 0, -1, false, MFS_GRAYED);
					grayed = true;
				}
				{
					HMENU setPresetMenu = CreatePopupMenu();
					AddPresetMenu(setPresetMenu, item->m_track, &item->m_presets);
					AddSubMenu(hMenu, setPresetMenu, __LOCALIZE("Set preset","sws_DLG_155"), -1, grayed ? MF_GRAYED : MFS_UNCHECKED);
				}
				AddToMenu(hMenu, __LOCALIZE("Clear FX presets","sws_DLG_155"), CLEAR_PRESETS_MSG, -1, grayed ? MF_GRAYED : MFS_UNCHECKED);
				break;
			case COL_ACTION_ON:
				AddToMenu(hMenu, __LOCALIZE("Set selected action (in the Actions window)","sws_DLG_155"), LEARN_ON_ACTION_MSG);
				AddToMenu(hMenu, __LOCALIZE("Clear actions","sws_DLG_155"), CLEAR_ON_ACTION_MSG);
				break;
			case COL_ACTION_OFF:
				AddToMenu(hMenu, __LOCALIZE("Set selected action (in the Actions window)","sws_DLG_155"), LEARN_OFF_ACTION_MSG);
				AddToMenu(hMenu, __LOCALIZE("Clear actions","sws_DLG_155"), CLEAR_OFF_ACTION_MSG);
				break;
		}

		// common menu items (if a row is selected)
		if (GetMenuItemCount(hMenu))
			AddToMenu(hMenu, SWS_SEPARATOR, 0);
		AddToMenu(hMenu, __LOCALIZE("Apply config","sws_DLG_155"), APPLY_MSG);
		AddToMenu(hMenu, __LOCALIZE("Preload config","sws_DLG_155"), PRELOAD_MSG);
		AddToMenu(hMenu, SWS_SEPARATOR, 0);
		AddToMenu(hMenu, __LOCALIZE("Copy configs","sws_DLG_155"), COPY_MSG);
		AddToMenu(hMenu, __LOCALIZE("Cut configs","sws_DLG_155"), CUT_MSG);
		AddToMenu(hMenu, __LOCALIZE("Paste configs","sws_DLG_155"), PASTE_MSG, -1, false, g_clipboardConfigs.GetSize() ? MF_ENABLED : MF_GRAYED);
		AddToMenu(hMenu, SWS_SEPARATOR, 0);
		AddToMenu(hMenu, __LOCALIZE("Insert config (shift rows down)","sws_DLG_155"), INS_DOWN_MSG);
		AddToMenu(hMenu, __LOCALIZE("Insert config (shift rows up)","sws_DLG_155"), INS_UP_MSG);
	}

	// useful when the wnd is resized (buttons not visible..)
	if (*wantDefaultItems)
	{
		char buf[64]="";
		_snprintfSafe(buf, sizeof(buf), __LOCALIZE_VERFMT("[Live Config #%d]","sws_DLG_155"), g_configId+1);
		AddToMenu(hMenu, buf, 0, -1, false, MF_GRAYED);
		AddToMenu(hMenu, __LOCALIZE("Create input track...","sws_DLG_155"), CREATE_INPUT_MSG);
		AddLearnMenu(hMenu, true);
		AddOptionsMenu(hMenu, true);
		AddOscCSurfMenu(hMenu, lc->m_osc, OSC_START_MSG, OSC_END_MSG);
/*JFB!! Live Configs user manual not up-to-date..
		AddToMenu(hMenu, SWS_SEPARATOR, 0);
		AddToMenu(hMenu, __LOCALIZE("Online help...","sws_DLG_155"), HELP_MSG);
*/
	}
	return hMenu;
}

int LiveConfigsWnd::OnKey(MSG* _msg, int _iKeyState) 
{
	if (_msg->message == WM_KEYDOWN)
	{
		if (!_iKeyState)
		{
			switch(_msg->wParam)
			{
				case VK_F2:		OnCommand(EDIT_DESC_MSG, 0);	return 1;
				case VK_RETURN: OnCommand(APPLY_MSG, 0);		return 1;
				case VK_DELETE:	OnCommand(CLEAR_CC_ROW_MSG, 0);	return 1;
				case VK_INSERT:	OnCommand(INS_DOWN_MSG, 0);		return 1;
			}
		}
		else if (_iKeyState == LVKF_CONTROL)
		{
			switch(_msg->wParam) {
				case 'X': OnCommand(CUT_MSG, 0);	return 1;
				case 'C': OnCommand(COPY_MSG, 0);	return 1;
				case 'V': OnCommand(PASTE_MSG, 0);	return 1;
			}
		}
	}
	return 0;
}

void LiveConfigsWnd::DrawControls(LICE_IBitmap* _bm, const RECT* _r, int* _tooltipHeight)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId);
	if (!lc)
		return;

	int x0=_r->left+SNM_GUI_X_MARGIN, h=SNM_GUI_TOP_H;
	if (_tooltipHeight)
		*_tooltipHeight = h;
	
	m_btnEnable.SetCheckState(lc->m_enable);
	if (SNM_AutoVWndPosition(DT_LEFT, &m_btnEnable, NULL, _r, &x0, _r->top, h, 4))
	{
		if (SNM_AutoVWndPosition(DT_LEFT, &m_cbConfig, &m_btnEnable, _r, &x0, _r->top, h))
		{
			if (SNM_AutoVWndPosition(DT_LEFT, &m_txtInputTr, NULL, _r, &x0, _r->top, h, 4))
			{
				if (SNM_AutoVWndPosition(DT_LEFT, &m_cbInputTr, &m_txtInputTr, _r, &x0, _r->top, h))
				{
					ColorTheme* ct = SNM_GetColorTheme();
					LICE_pixel col = ct ? LICE_RGBA_FROMNATIVE(ct->main_text,255) : LICE_RGBA(255,255,255,255);
					m_knobCC.SetFGColors(col, col);
					SNM_SkinKnob(&m_knobCC);
					if (SNM_AutoVWndPosition(DT_LEFT, &m_vwndCC, NULL, _r, &x0, _r->top, h))
					{
						m_knobFade.SetFGColors(col, col);
						SNM_SkinKnob(&m_knobFade);
						if (SNM_AutoVWndPosition(DT_LEFT, &m_vwndFade, NULL, _r, &x0, _r->top, h))
							SNM_AddLogo(_bm, _r, x0, h);
					}
				}
			}
		}
	}

	// 2nd row of controls (bottom)
	x0 = _r->left+SNM_GUI_X_MARGIN; h=SNM_GUI_BOT_H;
	int y0 = _r->bottom-h;

	SNM_SkinToolbarButton(&m_btnMonitor, __LOCALIZE("Monitor...","sws_DLG_155"));
	if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnMonitor, NULL, _r, &x0, y0, h, 4))
		return;

	SNM_SkinToolbarButton(&m_btnOptions, __LOCALIZE("Options","sws_DLG_155"));
	if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnOptions, NULL, _r, &x0, y0, h, 5))
		return;

	SNM_SkinToolbarButton(&m_btnLearn, __LOCALIZE("Learn","sws_DLG_155"));
	if (!SNM_AutoVWndPosition(DT_LEFT, &m_btnLearn, NULL, _r, &x0, y0, h, 4))
		return;
}

bool LiveConfigsWnd::GetToolTipString(int _xpos, int _ypos, char* _bufOut, int _bufOutSz)
{
	if (WDL_VWnd* v = m_parentVwnd.VirtWndFromPoint(_xpos,_ypos,1))
	{
		switch (v->GetID())
		{
			case BTNID_ENABLE:
				if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId))
					_snprintfSafe(_bufOut, _bufOutSz, __LOCALIZE_VERFMT("Live Config #%d: %s","sws_DLG_155"), g_configId+1, lc->m_enable?__LOCALIZE("on","sws_DLG_155"):__LOCALIZE("off","sws_DLG_155"));
				return true;
			case TXTID_INPUT_TRACK:
			case CMBID_INPUT_TRACK:
				_snprintfSafe(_bufOut, _bufOutSz, __LOCALIZE_VERFMT("Input track for Live Config #%d (optional)","sws_DLG_155"), g_configId+1);
				return true;
			case WNDID_CC_DELAY:
			case KNBID_CC_DELAY:
				// keep messages on a single line (for the langpack generator)
				lstrcpyn(_bufOut, __LOCALIZE("Optional delay before applying/preloading configs when receiving MIDI/OSC from a controller\nPrevents to be stuck: only the last stable value is processed, not all values in between","sws_DLG_155"), _bufOutSz);
				return true;
			case WNDID_FADE:
			case KNBID_FADE:
				// keep messages on a single line (for the langpack generator)
				lstrcpyn(_bufOut, __LOCALIZE("Optional fades out/in when deactivating/activating configs\nEnsures glitch-free switches","sws_DLG_155"), _bufOutSz);
				return true;
		}
	}
	return false;
}

bool LiveConfigsWnd::Insert(int _dir)
{
	if (!m_pLists.GetSize())
		return false;
	int sortCol = GetListView()->GetSortColumn();
	if (sortCol && sortCol!=1)
	{
		//JFB lazy here.. better than confusing the user..
		MessageBox(GetHWND(), __LOCALIZE("The list view must be sorted by ascending values!","sws_DLG_155"), __LOCALIZE("S&M - Error","sws_DLG_155"), MB_OK);
		return false;
	}
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(g_configId))
	{
		if (WDL_PtrList<LiveConfigItem>* ccConfs = &lc->m_ccConfs)
		{
			int pos=0;
			if (LiveConfigItem* item = (LiveConfigItem*)GetListView()->EnumSelected(&pos))
			{
				if (_dir>0)
				{
					pos--; // always >0, see EnumSelected()
//					if (pos==ccConfs->GetSize()-1) return false;
					for(int i=pos; i < ccConfs->GetSize()-1; i++) // <size-1!
						ccConfs->Get(i)->m_cc++;
					ccConfs->Insert(pos, new LiveConfigItem(pos));
				}
				else
				{
//					if (!pos) return false;
					ccConfs->Insert(pos, new LiveConfigItem(pos));
					for(int i=pos; i>0 ; i--) // >0 !
						ccConfs->Get(i)->m_cc--;
				}

				int delItemIdx = _dir>0 ? ccConfs->GetSize()-1 : 0;
				item = ccConfs->Get(delItemIdx);
				ccConfs->Delete(delItemIdx, false); // do not delete now (still displayed..)
				Update();
				Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);
				DELETE_NULL(item); // ok to del now that the update is done
				SelectByCCValue(g_configId, _dir>0 ? pos : pos-1);
				return true;
			}
		}
	}
	return false;
}


///////////////////////////////////////////////////////////////////////////////

void LiveConfigsUpdateEditorJob::Perform() {
	if (LiveConfigsWnd* w = g_lcWndMgr.Get())
		w->Update();
}

// moderate things a bit
void LiveConfigsUpdateFadeJob::Perform()
{
	if (m_value>0) // let the user pref as it is otherwise
		if (int* fadeLenPref = (int*)GetConfigVar("mutefadems10"))
			*fadeLenPref = m_value * 10;
}


///////////////////////////////////////////////////////////////////////////////
// project_config_extension_t
///////////////////////////////////////////////////////////////////////////////

static bool ProcessExtensionLine(const char *line, ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	LineParser lp(false);
	if (lp.parse(line) || lp.getnumtokens() < 2)
		return false;

	if (!strcmp(lp.gettoken_str(0), "<S&M_MIDI_LIVE"))
	{
		GUID g;	
		int configId = lp.gettoken_int(1) - 1;

		if (LiveConfig* lc = g_liveConfigs.Get()->Get(configId))
		{
			// default values should be consistent with the LiveConfig() constructor

			int success; // for asc. compatibility
			lc->m_enable = lp.gettoken_int(2);
			lc->m_version = lp.gettoken_int(3);
			lc->m_muteOthers = lp.gettoken_int(4);
			stringToGuid(lp.gettoken_str(5), &g);
			lc->SetInputTrackGUID(&g);
			lc->m_selScroll = lp.gettoken_int(6, &success);
			if (!success) lc->m_selScroll = 1;
			lc->m_offlineOthers = lp.gettoken_int(7, &success);
			if (!success) lc->m_offlineOthers = 0;
			lc->m_cc123 = lp.gettoken_int(8, &success);
			if (!success) lc->m_cc123 = 1;
			lc->m_ignoreEmpty = lp.gettoken_int(9, &success);
			if (!success) lc->m_ignoreEmpty = 0;
			lc->m_autoSends = lp.gettoken_int(10, &success);
			if (!success) lc->m_autoSends = 1;
			lc->m_ccDelay = lp.gettoken_int(11, &success);
			if (!success) lc->m_ccDelay = DEF_CC_DELAY;
			lc->m_fade = lp.gettoken_int(12, &success);
			if (!success)
			{
				if (int* fadeLenPref = (int*)GetConfigVar("mutefadems10"))
					lc->m_fade = int(*fadeLenPref/10 + 0.5);
				else
					lc->m_fade = DEF_FADE;
			}
			lc->m_osc = LoadOscCSurfs(NULL, lp.gettoken_str(13)); // NULL on err (e.g. "", token doesn't exist, etc.)

			if (isUndo)
			{
				lc->m_activeMidiVal = lp.gettoken_int(14, &success);
				if (!success) lc->m_activeMidiVal = -1;
				lc->m_curMidiVal = lp.gettoken_int(15, &success);
				if (!success) lc->m_curMidiVal = -1;
				lc->m_preloadMidiVal = lp.gettoken_int(16, &success);
				if (!success) lc->m_preloadMidiVal = -1;
				lc->m_curPreloadMidiVal = lp.gettoken_int(17, &success);
				if (!success) lc->m_curPreloadMidiVal = -1;
			}

			char linebuf[SNM_MAX_CHUNK_LINE_LENGTH]="";
			while(true)
			{
				if (!ctx->GetLine(linebuf,sizeof(linebuf)) && !lp.parse(linebuf))
				{
					if (lp.gettoken_str(0)[0] == '>')
						break;
					else if (LiveConfigItem* item = lc->m_ccConfs.Get(lp.gettoken_int(0)))
					{
						item->m_cc = lp.gettoken_int(0);
						item->m_desc.Set(lp.gettoken_str(1));
						stringToGuid(lp.gettoken_str(2), &g);
						item->m_track = GuidsEqual(&g, &GUID_NULL) ? NULL : GuidToTrack(&g); // GUID_NULL would affect the master track otherwise!
						item->m_trTemplate.Set(lp.gettoken_str(3));
						item->m_fxChain.Set(lp.gettoken_str(4));
						item->m_presets.Set(lp.gettoken_str(5));
						item->m_onAction.Set(lp.gettoken_str(6));
						item->m_offAction.Set(lp.gettoken_str(7));
						
						// upgrade if needed
						if (lc->m_version < 2) { // "2", not < LIVECFG_VERSION!
							UpgradePresetConfV1toV2(item->m_track, lp.gettoken_str(5), &item->m_presets);
							lc->m_version = LIVECFG_VERSION;
						}
					}
				}
				else
					break;
			}

			// refresh monitoring window + osc feedback
			UpdateMonitoring(
				configId,
				APPLY_MASK|PRELOAD_MASK,
				APPLY_MASK|PRELOAD_MASK);
		}

		// refresh editor
		if (LiveConfigsWnd* w = g_lcWndMgr.Get())
			w->Update();

		return true;
	}
	return false;
}

static void SaveExtensionConfig(ProjectStateContext *ctx, bool isUndo, struct project_config_extension_t *reg)
{
	GUID g; 
	char strId[128] = "";
	WDL_FastString escStrs[6];
	for (int i=0; i<g_liveConfigs.Get()->GetSize(); i++)
	{
		LiveConfig* lc = g_liveConfigs.Get()->Get(i);
		if (lc && !lc->IsDefault(false)) // avoid useless data in RPP files
		{
			// valid input track?
			MediaTrack* inputTr = lc->GetInputTrack();
			if (inputTr && CSurf_TrackToID(inputTr, false) >= 1) // excl. master
				guidToString(lc->GetInputTrackGUID(), strId);
			else
				guidToString((GUID*)&GUID_NULL, strId);

			if (lc->m_osc)
				makeEscapedConfigString(lc->m_osc->m_name.Get(), &escStrs[0]);
			else
				escStrs[0].Set("\"\"");

			ctx->AddLine("<S&M_MIDI_LIVE %d %d %d %d %s %d %d %d %d %d %d %d %s %d %d %d %d", 
				i+1,
				lc->m_enable,
				LIVECFG_VERSION,
				lc->m_muteOthers,
				strId,
				lc->m_selScroll,
				lc->m_offlineOthers,
				lc->m_cc123,
				lc->m_ignoreEmpty,
				lc->m_autoSends,
				lc->m_ccDelay,
				lc->m_fade,
				escStrs[0].Get(),
				lc->m_activeMidiVal,
				lc->m_curMidiVal,
				lc->m_preloadMidiVal,
				lc->m_curPreloadMidiVal);

			for (int j=0; j<lc->m_ccConfs.GetSize(); j++)
			{
				LiveConfigItem* item = lc->m_ccConfs.Get(j);
				if (item && !item->IsDefault(false)) // avoid useless data in RPP files
				{
					if (item->m_track && CSurf_TrackToID(item->m_track, false) > 0) 
						g = *(GUID*)GetSetMediaTrackInfo(item->m_track, "GUID", NULL);
					else 
						g = GUID_NULL;
					guidToString(&g, strId);

					makeEscapedConfigString(item->m_desc.Get(), &escStrs[0]);
					makeEscapedConfigString(item->m_trTemplate.Get(), &escStrs[1]);
					makeEscapedConfigString(item->m_fxChain.Get(), &escStrs[2]);
					makeEscapedConfigString(item->m_presets.Get(), &escStrs[3]);
					makeEscapedConfigString(item->m_onAction.Get(), &escStrs[4]);
					makeEscapedConfigString(item->m_offAction.Get(), &escStrs[5]);

					ctx->AddLine("%d %s %s %s %s %s %s %s", 
						item->m_cc, 
						escStrs[0].Get(), 
						strId, 
						escStrs[1].Get(),
						escStrs[2].Get(), 
						escStrs[3].Get(), 
						escStrs[4].Get(), 
						escStrs[5].Get());
				}
			}
			ctx->AddLine(">");
		}
	}
}

static void BeginLoadProjectState(bool isUndo, struct project_config_extension_t *reg)
{
	g_liveConfigs.Cleanup();

	while (g_liveConfigs.Get()->GetSize() < SNM_LIVECFG_NB_CONFIGS)
		g_liveConfigs.Get()->Add(new LiveConfig());

	for (int i=0; i<g_liveConfigs.Get()->GetSize(); i++)
		if (LiveConfig* lc = g_liveConfigs.Get()->Get(i))
			for (int j=0; j<lc->m_ccConfs.GetSize(); j++)
				if (LiveConfigItem* item = lc->m_ccConfs.Get(j))
					item->Clear(false);
}

static project_config_extension_t s_projectconfig = {
	ProcessExtensionLine, SaveExtensionConfig, BeginLoadProjectState, NULL
};


///////////////////////////////////////////////////////////////////////////////

void LiveConfigsSetTrackTitle() {
	ScheduledJob::Schedule(new LiveConfigsUpdateEditorJob(SNM_SCHEDJOB_ASYNC_DELAY_OPT));
}

// ScheduledJob because of multi-notifs
void LiveConfigsTrackListChange()
{
	// check consistency of all live configs
	for (int i=0; i<g_liveConfigs.Get()->GetSize(); i++)
	{
		if (LiveConfig* lc = g_liveConfigs.Get()->Get(i))
		{
			for (int j=0; j<lc->m_ccConfs.GetSize(); j++)
				if (LiveConfigItem* item = lc->m_ccConfs.Get(j))
					if (item->m_track && CSurf_TrackToID(item->m_track, false) <= 0)
						item->Clear(true);

			lc->SetInputTrack(lc->GetInputTrack(), lc->m_autoSends==1); // lc->GetInputTrack() can be NULL when deleted, etc..
		}
	}

	ScheduledJob::Schedule(new LiveConfigsUpdateEditorJob(SNM_SCHEDJOB_ASYNC_DELAY_OPT));
}


///////////////////////////////////////////////////////////////////////////////

int LiveConfigInit()
{
	GetPrivateProfileString("LiveConfigs", "BigFontName", SNM_DYN_FONT_NAME, g_lcBigFontName, sizeof(g_lcBigFontName), g_SNM_IniFn.Get());

	// instanciate the editor if needed, can be NULL
	g_lcWndMgr.Init();

	// instanciate monitors, if needed
	for (int i=0; i<SNM_LIVECFG_NB_CONFIGS; i++)
		g_monWndsMgr.Init(i);

	if (!plugin_register("projectconfig", &s_projectconfig))
		return 0;

	return 1;
}

void LiveConfigExit()
{
	plugin_register("-projectconfig", &s_projectconfig);
	WritePrivateProfileString("LiveConfigs", "BigFontName", g_lcBigFontName, g_SNM_IniFn.Get());
	g_lcWndMgr.Delete();
	g_monWndsMgr.DeleteAll();
}

void OpenLiveConfig(COMMAND_T*)
{
	if (LiveConfigsWnd* w = g_lcWndMgr.Create())
		w->Show(true, true);
}

int IsLiveConfigDisplayed(COMMAND_T*) {
	if (LiveConfigsWnd* w = g_lcWndMgr.Get())
		return w->IsWndVisible();
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Apply/preload configs
///////////////////////////////////////////////////////////////////////////////

// muting receives from the input track would be useless: the track is muted anyway
bool TimedMuteIfNeeded(MediaTrack* _tr, DWORD* _muteTime, int _fadeLen = -1)
{
	if (_fadeLen<0)
		if (int* fadeLenPref = (int*)GetConfigVar("mutefadems10"))
			_fadeLen = int(*fadeLenPref/10 + 0.5);

	bool mute = *(bool*)GetSetMediaTrackInfo(_tr, "B_MUTE", NULL);
	if (_fadeLen>0 && _tr && !mute)
	{
		GetSetMediaTrackInfo(_tr, "B_MUTE", &g_bTrue);
		*_muteTime = GetTickCount();
#ifdef _SNM_DEBUG
		char dbg[256] = "";
		_snprintfSafe(dbg, sizeof(dbg), "TimedMuteIfNeeded() - Muted: %s\n", (char*)GetSetMediaTrackInfo(_tr, "P_NAME", NULL));
		OutputDebugString(dbg);
#endif
	}
	return mute;
}

// no-op if _muteTime==0
void WaitForTinyFade(DWORD* _muteTime)
{
	int fadelen = 0;
	if (int* fadeLenPref = (int*)GetConfigVar("mutefadems10"))
		fadelen = int(*fadeLenPref/10 + 0.5);

	if (fadelen>0 && _muteTime && *_muteTime)
	{
		int sleeps = 0;
		while (int(GetTickCount()-*_muteTime) < fadelen)
		{
			if (sleeps > 1000) // timeout safety ~1s
				break;
#ifdef _WIN32
			// keep the UI updating
			MSG msg;
			while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
				DispatchMessage(&msg);
#endif
			Sleep(1);
			sleeps++;
		}
#ifdef _SNM_DEBUG
		char dbg[256] = "";
		_snprintfSafe(dbg, sizeof(dbg), "WaitForTrackMuteFade() - Approx wait time: %d ms\n", GetTickCount() - *_muteTime);
		OutputDebugString(dbg);
#endif
		*_muteTime = 0;
	}
}

void MuteAndInitCC123(LiveConfig* _lc, MediaTrack* _tr, DWORD* _muteTime, WDL_PtrList<void>* _muteTracks, WDL_PtrList<void>* _cc123Tracks, WDL_PtrList<bool>* _muteStates, bool _always = false)
{
	if (_always || _lc->m_fade>0 || _lc->m_cc123) // receives from the input track must be muted before sending all notes off
	{
		_muteStates->Add(TimedMuteIfNeeded(_tr, _muteTime, _always ? -1 : _lc->m_fade) ? &g_bTrue : &g_bFalse);
		_muteTracks->Add(_tr);
		if (_lc->m_cc123)
			_cc123Tracks->Add(_tr);
	}
}

void MuteAndInitCC123AllConfigs(LiveConfig* _lc, DWORD* _muteTime, WDL_PtrList<void>* _muteTracks, WDL_PtrList<void>* _cc123Tracks, WDL_PtrList<bool>* _muteStates)
{
	for (int i=0; i<_lc->m_ccConfs.GetSize(); i++)
		if (LiveConfigItem* item = _lc->m_ccConfs.Get(i))
			if (item->m_track && _muteTracks->Find(item->m_track) < 0)
/*JFB!! no! use-case ex: just switching fx presets (with all options disabled)
				MuteAndInitCC123(_lc, item->m_track, _muteTime, _muteTracks, _cc123Tracks, _muteStates, true); // always mute
*/
				MuteAndInitCC123(_lc, item->m_track, _muteTime, _muteTracks, _cc123Tracks, _muteStates);
}

// note: no tiny fades for sends unfortunately (last check v4.31) => fade dest tracks instead
void WaitForMuteAndSendCC123(LiveConfig* _lc, LiveConfigItem* _cfg, DWORD* _muteTime, WDL_PtrList<void>* _muteTracks, WDL_PtrList<void>* _cc123Tracks)
{
	WaitForTinyFade(_muteTime); // no-op if muteTime==0

	MediaTrack* inputTr = _lc->GetInputTrack();
	if (!inputTr || (inputTr && (_cfg->m_track != inputTr)))
		for (int i=0; i<_muteTracks->GetSize(); i++)
			MuteSends(inputTr, (MediaTrack*)_muteTracks->Get(i), true);

	if (_lc->m_cc123 && SendAllNotesOff(_cc123Tracks)) {
		WaitForAllNotesOff();
		_cc123Tracks->Empty();
	}
}

//  THE MEAT! HANDLE WITH CARE!
void ApplyPreloadLiveConfig(bool _apply, int _cfgId, int _val, LiveConfigItem* _lastCfg)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId);
	if (!lc) return;

	LiveConfigItem* cfg = lc->m_ccConfs.Get(_val);
	if (!cfg) return;

	// save selected tracks
	WDL_PtrList<MediaTrack> selTracks;
	SNM_GetSelectedTracks(NULL, &selTracks, true);

	// run desactivate action of the previous config *when it has no track*
	// we ensure that no track is selected when performing the action
	if (_apply && _lastCfg && !_lastCfg->m_track && _lastCfg->m_offAction.GetLength())
		if (int cmd = NamedCommandLookup(_lastCfg->m_offAction.Get()))
		{
			SNM_SetSelectedTrack(NULL, NULL, true, true);
			Main_OnCommand(cmd, 0);
			SNM_GetSelectedTracks(NULL, &selTracks, true); // selection may have changed
		}

	bool preloaded = (_apply && lc->m_preloadMidiVal>=0 && lc->m_preloadMidiVal==_val);
	if (cfg->m_track)
	{
		MediaTrack* inputTr = lc->GetInputTrack();

		// --------------------------------------------------------------------
		// 1) mute things a) to trigger tiny fades b) according to options
		// --------------------------------------------------------------------

		DWORD muteTime=0;
		WDL_PtrList<void> muteTracks, cc123Tracks;
		WDL_PtrList<bool> muteStates;

		// preloading?
		if (!_apply)
		{
			// mute things before reconfiguration (in order to trigger tiny fades, optional)
			// note: no preload on input track
			if (!inputTr || (inputTr && cfg->m_track != inputTr))
				MuteAndInitCC123(lc, cfg->m_track, &muteTime, &muteTracks, &cc123Tracks, &muteStates); 
		}

		// applying?
		// kinda repeating code patterns here, but maintaining all
		// possible combinaitions in a single loop was a nightmare..
		else 
		{	
			// mute things before reconfiguration
			MuteAndInitCC123(lc, cfg->m_track, &muteTime, &muteTracks, &cc123Tracks, &muteStates); 

			// mute (and later unmute) tracks to be set offline - optional
			if (lc->m_offlineOthers)
				for (int i=0; i<lc->m_ccConfs.GetSize(); i++)
					if (LiveConfigItem* item = lc->m_ccConfs.Get(i))
						if (item->m_track && 
							item->m_track != cfg->m_track && 
							(!inputTr || (inputTr && item->m_track != inputTr)) &&
							muteTracks.Find(item->m_track) < 0)
						{
							MuteAndInitCC123(lc, item->m_track, &muteTime, &muteTracks, &cc123Tracks, &muteStates); 
						}

			// first activation: cleanup *everything* as we do not know the initial state
			if (!_lastCfg)
			{
				MuteAndInitCC123AllConfigs(lc, &muteTime, &muteTracks, &cc123Tracks, &muteStates);
			}
			else
			{
				if (_lastCfg->m_track && _lastCfg->m_track != cfg->m_track && muteTracks.Find(_lastCfg->m_track) < 0)
				{
					if (!inputTr || (inputTr && _lastCfg->m_track != inputTr))
						MuteAndInitCC123(lc, _lastCfg->m_track, &muteTime, &muteTracks, &cc123Tracks, &muteStates);
					else if (inputTr && _lastCfg->m_track == inputTr) // corner case fix
						MuteAndInitCC123AllConfigs(lc, &muteTime, &muteTracks, &cc123Tracks, &muteStates);
				}
			}

			// end with mute states that will not be restored (user option)
			if (lc->m_muteOthers && (!inputTr || (inputTr && cfg->m_track != inputTr)))
			{
				for (int i=0; i<lc->m_ccConfs.GetSize(); i++)
					if (LiveConfigItem* item = lc->m_ccConfs.Get(i))
						if (item->m_track && 
							item->m_track != cfg->m_track && 
							(!inputTr || (inputTr && item->m_track != inputTr)))
						{
							TimedMuteIfNeeded(item->m_track, &muteTime, -1); // -1 => always obey
							int idx = muteTracks.Find(item->m_track);
							if (idx<0)
							{
								muteTracks.Add(item->m_track);
								if (lc->m_cc123)
									cc123Tracks.Add(item->m_track);
							}
							else
							{
								muteStates.Delete(idx, false);
								muteStates.Insert(idx, NULL);
							}
						}
			}
		}

		// --------------------------------------------------------------------
		// 2) reconfiguration
		// --------------------------------------------------------------------

		// run desactivate action of the deactivated config if it has a track
		// when performing the action, we ensure that the only selected track is the deactivated track
		if (_apply && _lastCfg && _lastCfg->m_track && _lastCfg->m_offAction.GetLength())
			if (int cmd = NamedCommandLookup(_lastCfg->m_offAction.Get()))
			{
				WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);

				SNM_SetSelectedTrack(NULL, _lastCfg->m_track, true, true);
				Main_OnCommand(cmd, 0);
				SNM_GetSelectedTracks(NULL, &selTracks, true); // selection may have changed
			}


		// reconfiguration via state updates
		if (!preloaded)
		{
			// apply tr template (preserves routings, folder states, etc..)
			// if the altered track has sends, it'll be glitch free too as me mute this source track
			if (cfg->m_trTemplate.GetLength()) 
			{
				WDL_FastString tmplt; char fn[SNM_MAX_PATH] = "";
				GetFullResourcePath("TrackTemplates", cfg->m_trTemplate.Get(), fn, sizeof(fn));
				if (LoadChunk(fn, &tmplt) && tmplt.GetLength())
				{
					SNM_SendPatcher p(cfg->m_track); // auto-commit on destroy
					
					WDL_FastString chunk; MakeSingleTrackTemplateChunk(&tmplt, &chunk, true, true, false);
					if (ApplyTrackTemplate(cfg->m_track, &chunk, false, false, &p))
					{
						// make sure the track will be restored with its current name 
						WDL_FastString trNameEsc("");
						if (char* name = (char*)GetSetMediaTrackInfo(cfg->m_track, "P_NAME", NULL))
							if (*name)
								makeEscapedConfigString(name, &trNameEsc);
						p.ParsePatch(SNM_SET_CHUNK_CHAR,1,"TRACK","NAME",0,1,(void*)trNameEsc.Get());

						// make sure the track will be restored with proper mute state
						char onoff[2]; strcpy(onoff, *(bool*)GetSetMediaTrackInfo(cfg->m_track, "B_MUTE", NULL) ? "1" : "0");
						p.ParsePatch(SNM_SET_CHUNK_CHAR,1,"TRACK","MUTESOLO",0,1,onoff);

						WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);
					}
				} // auto-commit
			}
			// fx chain reconfiguration via state chunk update
			else if (cfg->m_fxChain.GetLength())
			{
				char fn[SNM_MAX_PATH]=""; WDL_FastString chunk;
				GetFullResourcePath("FXChains", cfg->m_fxChain.Get(), fn, sizeof(fn));
				if (LoadChunk(fn, &chunk) && chunk.GetLength())
				{
					SNM_FXChainTrackPatcher p(cfg->m_track); // auto-commit on destroy
					if (p.SetFXChain(&chunk))
						WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);
				}
			} // auto-commit

		} // if (!preloaded)


		// make sure fx are online for the activated/preloaded track
		// done here because fx may have been set offline via state loading above
		if ((!_apply ||	(_apply && lc->m_offlineOthers)) && 
			(!inputTr || (inputTr && cfg->m_track!=inputTr)))
		{
			if (!preloaded && TrackFX_GetCount(cfg->m_track))
			{
				SNM_ChunkParserPatcher p(cfg->m_track);
				p.SetWantsMinimalState(true); // ok 'cause read-only
				
				 // some fx offline on the activated track? 
				// macro-ish but better than pushing a new state
				char zero[2] = "0";
				if (!p.Parse(SNM_GETALL_CHUNK_CHAR_EXCEPT, 2, "FXCHAIN", "BYPASS", 0xFFFF, 2, zero))
				{
					WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);
					SNM_SetSelectedTrack(NULL, cfg->m_track, true, true);
					Main_OnCommand(40536, 0); // online
				}
			}
		}

		// offline others but active/preloaded tracks
		if (_apply && lc->m_offlineOthers && (!inputTr || (inputTr && cfg->m_track!=inputTr)))
		{
			MediaTrack* preloadTr = NULL;
			if (preloaded && _lastCfg)
				preloadTr = _lastCfg->m_track; // because preload & current are swapped
			else if (LiveConfigItem* preloadCfg = lc->m_ccConfs.Get(lc->m_preloadMidiVal)) // can be <0
				preloadTr = preloadCfg->m_track;

			// select tracks to be set offline (already muted above)
			SNM_SetSelectedTrack(NULL, NULL, true, true);
			for (int i=0; i<lc->m_ccConfs.GetSize(); i++)
				if (LiveConfigItem* item = lc->m_ccConfs.Get(i))
					if (item->m_track && 
						item->m_track != cfg->m_track && // excl. the activated track
						(!inputTr || (inputTr && item->m_track != inputTr)) && // excl. the input track
						(!preloadTr || (preloadTr && preloadTr != item->m_track))) // excl. the preloaded track
					{
						GetSetMediaTrackInfo(item->m_track, "I_SELECTED", &g_i1);
					}
		
			WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);

			// set all fx offline for sel tracks, no-op if already offline
			// macro-ish but better than using a SNM_ChunkParserPatcher for each track..
			Main_OnCommand(40535, 0);
			Main_OnCommand(41204, 0); // fully unload unloaded VSTs
		}


		// track reconfiguration: fx presets
		// note: exclusive vs template/fx chain but done here because fx may have been set online just above
		if (!preloaded && cfg->m_presets.GetLength())
		{
			WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);
			TriggerFXPresets(cfg->m_track, &(cfg->m_presets));
		}

		// perform activate action
		if (_apply && cfg->m_onAction.GetLength())
			if (int cmd = NamedCommandLookup(cfg->m_onAction.Get()))
			{
				WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);
				SNM_SetSelectedTrack(NULL, cfg->m_track, true, true);
				Main_OnCommand(cmd, 0);
				SNM_GetSelectedTracks(NULL, &selTracks, true); // selection may have changed
			}


		// --------------------------------------------------------------------
		// 3) unmute things
		// --------------------------------------------------------------------

		WaitForMuteAndSendCC123(lc, cfg, &muteTime, &muteTracks, &cc123Tracks);

		if (!_apply)
		{
			if (muteStates.Get(0)) // just in case..
				GetSetMediaTrackInfo(cfg->m_track, "B_MUTE", muteStates.Get(0));
		}
		else
		{
			// unmute all sends from the input track except those sending to the new active track
			if (inputTr && inputTr!=cfg->m_track)
			{
				int sndIdx=0;
				MediaTrack* destTr = (MediaTrack*)GetSetTrackSendInfo(inputTr, 0, sndIdx, "P_DESTTRACK", NULL);
				while(destTr)
				{
					bool mute = (destTr!=cfg->m_track);
					if (*(bool*)GetSetTrackSendInfo(inputTr, 0, sndIdx, "B_MUTE", NULL) != mute)
						GetSetTrackSendInfo(inputTr, 0, sndIdx, "B_MUTE", &mute);
					destTr = (MediaTrack*)GetSetTrackSendInfo(inputTr, 0, ++sndIdx, "P_DESTTRACK", NULL);
				}
			}

			// restore mute states, if needed
			for (int i=0; i < muteTracks.GetSize(); i++)
				if (MediaTrack* tr = (MediaTrack*)muteTracks.Get(i))
					if (tr != cfg->m_track && tr != inputTr)
						if (bool* mute = muteStates.Get(i))
							if (*(bool*)GetSetMediaTrackInfo(tr, "B_MUTE", NULL) != *mute)
								GetSetMediaTrackInfo(tr, "B_MUTE", mute);

			// unmute the input track, whatever is lc->m_muteOthers
			if (inputTr)
				if (*(bool*)GetSetMediaTrackInfo(inputTr, "B_MUTE", NULL))
					GetSetMediaTrackInfo(inputTr, "B_MUTE", &g_bFalse);

			// unmute the config track, whatever is lc->m_muteOthers
			if (*(bool*)GetSetMediaTrackInfo(cfg->m_track, "B_MUTE", NULL))
				GetSetMediaTrackInfo(cfg->m_track, "B_MUTE", &g_bFalse);
		}

	} // if (cfg->m_track)
	else
	{
		// perform activate action
		if (_apply && cfg->m_onAction.GetLength())
		{
			if (int cmd = NamedCommandLookup(cfg->m_onAction.Get()))
			{
				SNM_SetSelectedTrack(NULL, NULL, true, true);
				Main_OnCommand(cmd, 0);
				SNM_GetSelectedTracks(NULL, &selTracks, true); // selection may have changed
			}
		}
	}

	// restore selected tracks
	SNM_SetSelectedTracks(NULL, &selTracks, true, true);
}


///////////////////////////////////////////////////////////////////////////////
// Apply configs
///////////////////////////////////////////////////////////////////////////////

void ApplyLiveConfig(int _cfgId, int _val, bool _immediate, int _valhw, int _relmode)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId);
	if (lc && lc->m_enable) 
	{
		ScheduledJob::Schedule(
			new ApplyLiveConfigJob(_cfgId, _immediate ? 0 : lc->m_ccDelay, _val, _valhw, _relmode));

		// => lc->m_curMidiVal is updated via ApplyLiveConfigJob::Init()
		//    (cannot do things like lc->m_curMidiVal = job->GetValue();
		//    as the job may already be destroyed at this point)
	}
}

void ApplyLiveConfigJob::Init(ScheduledJob* _replacedJob)
{
	MidiOscActionJob::Init(_replacedJob);
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId))
	{
		lc->m_curMidiVal = GetIntValue();

		// ui/osc update: value is "changing" (e.g. grayed in monitors)
		// no editor update though, does not display "changing" values only "solid" ones
		if (!IsImmediate())
			UpdateMonitoring(m_cfgId, APPLY_MASK, 0); // ui/osc update: "changing"
	}
}

void ApplyLiveConfigJob::Perform()
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId);
	if (!lc) return;

	Undo_BeginBlock2(NULL);

	// swap preload/current configs?
	int absval = GetIntValue();
	bool preloaded = (lc->m_preloadMidiVal>=0 && lc->m_preloadMidiVal==absval);

	LiveConfigItem* cfg = lc->m_ccConfs.Get(absval);
	if (cfg && lc->m_enable && 
		absval!=lc->m_activeMidiVal &&
		(!lc->m_ignoreEmpty || (lc->m_ignoreEmpty && !cfg->IsDefault(true)))) // ignore empty configs
	{
		LiveConfigItem* lastCfg = lc->m_ccConfs.Get(lc->m_activeMidiVal); // can be <0
		if (!lastCfg || (lastCfg && !lastCfg->Equals(cfg, true)))
		{
			PreventUIRefresh(1);
			ApplyPreloadLiveConfig(true, m_cfgId, absval, lastCfg);
			PreventUIRefresh(-1);
		} 

		// done
		if (preloaded) {
			lc->m_preloadMidiVal = lc->m_curPreloadMidiVal = lc->m_activeMidiVal;
			lc->m_activeMidiVal = lc->m_curMidiVal = absval;
		}
		else
			lc->m_activeMidiVal = absval;
	}

	{
		char buf[SNM_MAX_ACTION_NAME_LEN]="";
		_snprintfSafe(buf, sizeof(buf), __LOCALIZE_VERFMT("Apply Live Config %d, value %d","sws_undo"), m_cfgId+1, absval);
		Undo_EndBlock2(NULL, buf, UNDO_STATE_ALL);
	}

	// update GUIs in any case, e.g. tweaking (gray cc value) to same value (=> black)
	if (LiveConfigsWnd* w = g_lcWndMgr.Get()) {
		w->Update();
//		w->SelectByCCValue(m_cfgId, lc->m_activeMidiVal);
	}

	// swap preload/current configs => update both preload & current panels
	UpdateMonitoring(
		m_cfgId,
		APPLY_MASK | (preloaded ? PRELOAD_MASK : 0), 
		APPLY_MASK | (preloaded ? PRELOAD_MASK : 0));
}

double ApplyLiveConfigJob::GetCurrentValue() {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId))
		return lc->m_curMidiVal;
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Preload configs
///////////////////////////////////////////////////////////////////////////////

void PreloadLiveConfig(int _cfgId, int _val, bool _immediate, int _valhw, int _relmode)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId);
	if (lc && lc->m_enable)
	{
		ScheduledJob::Schedule(
			new PreloadLiveConfigJob(_cfgId, _immediate ? 0 : lc->m_ccDelay, _val, _valhw, _relmode));

		// => lc->m_curPreloadMidiVal is updated via PreloadLiveConfigJob::Init()
	}
}

void PreloadLiveConfigJob::Init(ScheduledJob* _replacedJob)
{
	MidiOscActionJob::Init(_replacedJob);
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId))
	{
		lc->m_curPreloadMidiVal = GetIntValue();

		// ui/osc update
		if (!IsImmediate())
			UpdateMonitoring(m_cfgId, PRELOAD_MASK, 0); 
	}
}

void PreloadLiveConfigJob::Perform()
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId);
	if (!lc) return;

	Undo_BeginBlock2(NULL);

	int absval = GetIntValue();
	MediaTrack* inputTr = lc->GetInputTrack();
	LiveConfigItem* cfg = lc->m_ccConfs.Get(absval);
	LiveConfigItem* lastCfg = lc->m_ccConfs.Get(lc->m_activeMidiVal); // can be <0
	if (cfg && lc->m_enable && 
		absval!=lc->m_preloadMidiVal &&
		(!lc->m_ignoreEmpty || (lc->m_ignoreEmpty && !cfg->IsDefault(true))) && // ignore empty configs
		(!lastCfg || (lastCfg && (!cfg->m_track || !lastCfg->m_track || cfg->m_track!=lastCfg->m_track)))) // ignore preload over the active track
	{
		LiveConfigItem* lastPreloadCfg = lc->m_ccConfs.Get(lc->m_preloadMidiVal); // can be <0
		if (cfg->m_track && // ATM preload only makes sense for configs for which a track is defined
/*JFB no, always obey!
			lc->m_offlineOthers &&
*/
			(!inputTr || (inputTr && cfg->m_track!=inputTr)) && // no preload for the input track
			(!lastCfg || (lastCfg && !lastCfg->Equals(cfg, true))) &&
			(!lastPreloadCfg || (lastPreloadCfg && !lastPreloadCfg->Equals(cfg, true))))
		{
			PreventUIRefresh(1);
			ApplyPreloadLiveConfig(false, m_cfgId, absval, lastCfg);
			PreventUIRefresh(-1);
		}

		// done
		lc->m_preloadMidiVal = absval;
	}

	{
		char buf[SNM_MAX_ACTION_NAME_LEN]="";
		_snprintfSafe(buf, sizeof(buf), __LOCALIZE_VERFMT("Preload Live Config %d, value: %d","sws_undo"), m_cfgId+1, absval);
		Undo_EndBlock2(NULL, buf, UNDO_STATE_ALL);
	}

	// update GUIs/OSC in any case
	if (LiveConfigsWnd* w = g_lcWndMgr.Get()) {
		w->Update();
//		w->SelectByCCValue(m_cfgId, lc->m_preloadMidiVal);
	}
	UpdateMonitoring(m_cfgId, PRELOAD_MASK, PRELOAD_MASK);
}

double PreloadLiveConfigJob::GetCurrentValue() {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId))
		return lc->m_curPreloadMidiVal;
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Monitor window update + OSC feedback
///////////////////////////////////////////////////////////////////////////////

// _flags: &1=ui update, &2=osc feedback
// _val: -2=disabled, -1=no active/preload value, >=0 "normal" value 
void UpdateMonitor(int _cfgId, int _idx, int _val, bool _changed, int _flags)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId);
	if (!lc) return;

	bool done = false;
	if (_val>=-1)
	{
		WDL_FastString info1, info2;
		if (_val>=0)
			info1.SetFormatted(ELLIPSIZE, "%03d", _val);

		if (LiveConfigItem* item = lc->m_ccConfs.Get(_val))
			item->GetInfo(&info2);

		if (_flags&1)
		{
			if (LiveConfigMonitorWnd* w = g_monWndsMgr.Get(_cfgId))
			{
				w->GetMonitors()->SetText(_idx, info1.Get(), 0, _changed ? 255 : 143);
				w->GetMonitors()->SetText(_idx+1, info2.Get(), 0, _changed ? 255 : 143);
				done = true;
			}
		}

		if (_flags&2 && lc->m_osc)
		{
			if (info1.GetLength() && info2.GetLength())
				info1.Append(" ");
			info1.Append(&info2);

			lc->m_osc->SendStr(_idx==1 ? 
					(_changed ? APPLY_OSC : APPLYING_OSC) :
					(_changed ? PRELOAD_OSC : PRELOADING_OSC),
				info1.Get(),
				_cfgId+1);
			done = true;
		}
	}

	if (!done)
	{
		if (_flags&1)
		{
			if (LiveConfigMonitorWnd* w = g_monWndsMgr.Get(_cfgId))
			{
				w->GetMonitors()->SetText(_idx, "---", SNM_COL_RED_MONITOR);
				w->GetMonitors()->SetText(_idx+1, __LOCALIZE("<DISABLED>","sws_DLG_169"), SNM_COL_RED_MONITOR);
			}
		}

		if (_flags&2 && lc->m_osc)
		{
			lc->m_osc->SendStr(
				_idx==1?APPLY_OSC:PRELOAD_OSC,
				__LOCALIZE("<DISABLED>","sws_DLG_169"),
				_cfgId+1);
		}
	}
}

// _flags: &1=ui update, &2=osc feedback
void UpdateMonitoring(int _cfgId, int _whatFlags, int _commitFlags, int _flags)
{
	LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId);
	LiveConfigMonitorWnd* monWnd = g_monWndsMgr.Get(_cfgId);
	if (!lc || (!lc->m_osc && !monWnd))
		return;

	if (lc->m_enable)
	{
		if (_whatFlags & APPLY_MASK)
		{
			bool changed = (_commitFlags & APPLY_MASK); // otherwise: changing
			UpdateMonitor(_cfgId, 1, changed ? lc->m_activeMidiVal : lc->m_curMidiVal, changed, _flags);
		}
		if (_whatFlags & PRELOAD_MASK)
		{
			bool preloaded = (_commitFlags&PRELOAD_MASK) == PRELOAD_MASK; // otherwise: preloading (== to fix C4800)
			UpdateMonitor(_cfgId, 3, preloaded ? lc->m_preloadMidiVal : lc->m_curPreloadMidiVal, preloaded, _flags);

			// force display of the preload area
			if ((preloaded ? lc->m_preloadMidiVal : lc->m_curPreloadMidiVal) >= 0 &&
				_flags&1 && monWnd && monWnd->GetMonitors()->GetRows()<2)
			{
				monWnd->GetMonitors()->SetRows(2);
			}
		}
	}
	else
	{
		UpdateMonitor(_cfgId, 1, -2, true, _flags); // -1 to force err display
		UpdateMonitor(_cfgId, 3, -2, true, _flags); // -1 to force err display
	}
}


///////////////////////////////////////////////////////////////////////////////
// LiveConfigMonitorWnd (multiple instances!)
///////////////////////////////////////////////////////////////////////////////

enum {
  WNDID_MONITORS=2000, //JFB would be great to have _APS_NEXT_CONTROL_VALUE *always* defined
  TXTID_MON0,
  TXTID_MON1,
  TXTID_MON2,
  TXTID_MON3,
  TXTID_MON4
};

LiveConfigMonitorWnd::LiveConfigMonitorWnd(int _cfgId)
	: SWS_DockWnd() // must use the default constructor to create multiple instances
{
	m_cfgId = _cfgId;

	char title[64]="", dockId[64]="";
	_snprintfSafe(title, sizeof(title), __LOCALIZE_VERFMT("Live Config #%d Monitor","sws_DLG_169"), m_cfgId+1);
	_snprintfSafe(dockId, sizeof(dockId), LIVECFG_MON_WND_ID, m_cfgId+1);

	// see SWS_DockWnd(): default init for other member vars
	m_iResource=IDD_SNM_LIVE_CONFIG_MON;
	m_wndTitle.Set(title);
	m_id.Set(dockId);
	m_iCmdID = SWSGetCommandID(OpenLiveConfigMonitorWnd, (INT_PTR)m_cfgId);

	// screensets: already registered via SNM_WindowManager::Init()

	// Must call SWS_DockWnd::Init() to restore parameters and open the window if necessary
	Init();
}

LiveConfigMonitorWnd::~LiveConfigMonitorWnd()
{
	m_mons.RemoveAllChildren(false);
	m_mons.SetRealParent(NULL);
}

void LiveConfigMonitorWnd::OnInitDlg()
{
	m_vwnd_painter.SetGSC(WDL_STYLE_GetSysColor);
	m_parentVwnd.SetRealParent(m_hwnd);

	for (int i=0; i<5; i++)
		m_txtMon[i].SetID(TXTID_MON0+i);
	m_mons.AddMonitors(&m_txtMon[0], &m_txtMon[1], &m_txtMon[2], &m_txtMon[3], &m_txtMon[4]);
	m_mons.SetID(WNDID_MONITORS);
	m_mons.SetRows(1);
	m_parentVwnd.AddChild(&m_mons);

	{
		char buf[64]="";
		_snprintfSafe(buf, sizeof(buf), __LOCALIZE_VERFMT("Live Config #%d","sws_DLG_169"), m_cfgId+1);
		m_mons.SetTitles(__LOCALIZE("CURRENT","sws_DLG_169"), buf, __LOCALIZE("PRELOAD","sws_DLG_169"), " "); // " " trick to get a lane
		_snprintfSafe(buf, sizeof(buf), "#%d", m_cfgId+1);
		m_mons.SetFontName(g_lcBigFontName);
		m_mons.SetText(0, buf, 0, 16);
	}
	
	UpdateMonitoring(
		m_cfgId,
		APPLY_MASK|PRELOAD_MASK,
		APPLY_MASK|PRELOAD_MASK,
		1); // ui only
}

void LiveConfigMonitorWnd::OnDestroy() {
	m_mons.RemoveAllChildren(false);
	m_mons.SetRealParent(NULL);
}

INT_PTR LiveConfigMonitorWnd::OnUnhandledMsg(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_MOUSEWHEEL:
		{
			int val = (short)HIWORD(wParam);
#ifdef _SNM_DEBUG
			char dbg[256] = "";
			_snprintfSafe(dbg, sizeof(dbg), "WM_MOUSEWHEEL - val: %d\n", val);
			OutputDebugString(dbg);
#endif
			if (WDL_VWnd* mon0 = m_parentVwnd.GetChildByID(TXTID_MON0))
			{
				bool vis = mon0->IsVisible();
				mon0->SetVisible(false); // just a setter.. to exclude from VirtWndFromPoint()

				POINT p; GetCursorPos(&p);
				ScreenToClient(m_hwnd, &p);
				if (WDL_VWnd* v = m_parentVwnd.VirtWndFromPoint(p.x,p.y,1))
				{
					switch (v->GetID())
					{
						case TXTID_MON1:
						case TXTID_MON2:
							if (val>0) NextPreviousLiveConfig(m_cfgId, false, -1);
							else NextPreviousLiveConfig(m_cfgId, false, 1);
							break;
						case TXTID_MON3:
						case TXTID_MON4:
							if (val>0) NextPreviousLiveConfig(m_cfgId, true, -1);
							else NextPreviousLiveConfig(m_cfgId, true, 1);
							break;
					}
				}
				mon0->SetVisible(vis);
			}
		}
		return -1;
	}
	return 0;
}

void LiveConfigMonitorWnd::DrawControls(LICE_IBitmap* _bm, const RECT* _r, int* _tooltipHeight)
{
	m_mons.SetPosition(_r);
	m_mons.SetVisible(true);
	SNM_AddLogo(_bm, _r);
}

// handle mouse up/down events at window level (rather than inheriting VWnds)
int LiveConfigMonitorWnd::OnMouseDown(int _xpos, int _ypos)
{
	if (m_parentVwnd.VirtWndFromPoint(_xpos,_ypos,1) != NULL)
		return 1;
	return 0;
}

bool LiveConfigMonitorWnd::OnMouseUp(int _xpos, int _ypos)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(m_cfgId))
	{
		if (WDL_VWnd* mon0 = m_parentVwnd.GetChildByID(TXTID_MON0))
		{
			bool vis = mon0->IsVisible();
			mon0->SetVisible(false); // just a setter.. to exclude from VirtWndFromPoint()
			if (WDL_VWnd* v = m_parentVwnd.VirtWndFromPoint(_xpos,_ypos,1))
				switch (v->GetID())
				{
					// tgl between 1 & 2 rows
					case TXTID_MON1:
					case TXTID_MON2:
						m_mons.SetRows(m_mons.GetRows()==1 ? 2 : 1);
						break;
					// preload
					case TXTID_MON3:
					case TXTID_MON4:
						if (lc->m_preloadMidiVal>=0)
							ApplyLiveConfig(m_cfgId, lc->m_preloadMidiVal, true); // immediate
						break;
				}
			mon0->SetVisible(vis);
		}
	}
	return true;
}

void OpenLiveConfigMonitorWnd(int _idx)
{
	if (_idx>=0 && _idx<SNM_LIVECFG_NB_CONFIGS)
		if (LiveConfigMonitorWnd* w = g_monWndsMgr.Create(_idx))
			w->Show(true, true);
}

void OpenLiveConfigMonitorWnd(COMMAND_T* _ct) {
	OpenLiveConfigMonitorWnd((int)_ct->user);
}

int IsLiveConfigMonitorWndDisplayed(COMMAND_T* _ct)
{
	int wndId = (int)_ct->user;
	if (wndId>=0 && wndId<SNM_LIVECFG_NB_CONFIGS)
		if (LiveConfigMonitorWnd* w = g_monWndsMgr.Get(wndId))
			return w->IsWndVisible();
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Actions
///////////////////////////////////////////////////////////////////////////////

void ApplyLiveConfig(MIDI_COMMAND_T* _ct, int _val, int _valhw, int _relmode, HWND _hwnd) {
	ApplyLiveConfig((int)_ct->user, _val, false, _valhw, _relmode);
}

void PreloadLiveConfig(MIDI_COMMAND_T* _ct, int _val, int _valhw, int _relmode, HWND _hwnd) {
	PreloadLiveConfig((int)_ct->user, _val, false, _valhw, _relmode);
}

void SwapCurrentPreloadLiveConfigs(COMMAND_T* _ct)
{
	int cfgId = (int)_ct->user;
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(cfgId))
		ApplyLiveConfig(cfgId, lc->m_preloadMidiVal, true); // immediate
}

void ApplyNextLiveConfig(COMMAND_T* _ct) {
	NextPreviousLiveConfig((int)_ct->user, false, 1);
}

void ApplyPreviousLiveConfig(COMMAND_T* _ct) {
	NextPreviousLiveConfig((int)_ct->user, false, -1);
}

void PreloadNextLiveConfig(COMMAND_T* _ct) {
	NextPreviousLiveConfig((int)_ct->user, true, 1);
}

void PreloadPreviousLiveConfig(COMMAND_T* _ct) {
	NextPreviousLiveConfig((int)_ct->user, true, -1);
}

// not based on m_activeMidiVal: cc "smoothing" must apply here too!
// init cases lc->m_curMidiVal==-1 or lc->m_curPreloadMidiVal==-1 are properly manged
bool NextPreviousLiveConfig(int _cfgId, bool _preload, int _step)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId))
	{
		// 1st try
		int startMidiVal = (_preload ? lc->m_curPreloadMidiVal : lc->m_curMidiVal);
		if (startMidiVal<0)
			startMidiVal = _step>0 ? -1 : lc->m_ccConfs.GetSize();

		int i = startMidiVal + _step;
		while (_step>0 ? i<lc->m_ccConfs.GetSize() : i>=0)
		{
			if (LiveConfigItem* item = lc->m_ccConfs.Get(i))
			{
				// IsDefault(false): "comments-only" configs can be useful (osc feedback)
				if (!lc->m_ignoreEmpty || (lc->m_ignoreEmpty && !item->IsDefault(false)))
				{
					if (_preload) PreloadLiveConfig(_cfgId, i, false);
					else ApplyLiveConfig(_cfgId, i, false);
					return true;
				}
			}
			i += _step;
		}

		// 2nd try: cycle up/down
		startMidiVal = (_preload ? lc->m_curPreloadMidiVal : lc->m_curMidiVal);
		if (startMidiVal<0)
			startMidiVal = _step>0 ? 0 : lc->m_ccConfs.GetSize()-1;

		i = _step>0 ? -1 + _step : lc->m_ccConfs.GetSize() + _step;
		while (_step>0 ? i < startMidiVal : i > startMidiVal)
		{
			if (LiveConfigItem* item = lc->m_ccConfs.Get(i))
			{
				if (!lc->m_ignoreEmpty || (lc->m_ignoreEmpty && !item->IsDefault(false)))
				{
					if (_preload) PreloadLiveConfig(_cfgId, i, false);
					else ApplyLiveConfig(_cfgId, i, false);
					return true;
				}
			}
			i += _step;
		}
	}
	return false;
}

/////

int IsLiveConfigEnabled(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		return (lc->m_enable == 1);
	return false;
}

// _val: 0, 1 or <0 to toggle
void UpdateEnableLiveConfig(int _cfgId, int _val)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get(_cfgId))
	{
		lc->m_enable = _val<0 ? !lc->m_enable : _val;
		if (!lc->m_enable)
			lc->m_curMidiVal = lc->m_activeMidiVal = lc->m_curPreloadMidiVal = lc->m_preloadMidiVal = -1;
		Undo_OnStateChangeEx2(NULL, UNDO_STR, UNDO_STATE_MISCCFG, -1);

		if (g_configId == _cfgId)
			if (LiveConfigsWnd* w = g_lcWndMgr.Get())
				w->Update();
		UpdateMonitoring(_cfgId, APPLY_MASK|PRELOAD_MASK, 0);

		// RefreshToolbar()) not required..
	}
}

void EnableLiveConfig(COMMAND_T* _ct) {
	UpdateEnableLiveConfig((int)_ct->user, 1);
}

void DisableLiveConfig(COMMAND_T* _ct) {
	UpdateEnableLiveConfig((int)_ct->user, 0);
}

void ToggleEnableLiveConfig(COMMAND_T* _ct) {
	UpdateEnableLiveConfig((int)_ct->user, -1);
}

/////

int IsMuteOthersLiveConfigEnabled(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		return (lc->m_muteOthers != 0);
	return false;
}

// _val: 0, 1 or <0 to toggle
// gui refresh not needed (only in ctx menu)
void UpdateMuteOthers(COMMAND_T* _ct, int _val)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user)) {
		lc->m_muteOthers = _val<0 ? !lc->m_muteOthers : _val;
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(_ct), UNDO_STATE_MISCCFG, -1);
		// RefreshToolbar()) not required..
	}
}

void EnableMuteOthersLiveConfig(COMMAND_T* _ct) {
	UpdateMuteOthers(_ct, 1);
}

void DisableMuteOthersLiveConfig(COMMAND_T* _ct) {
	UpdateMuteOthers(_ct, 0);
}

void ToggleMuteOthersLiveConfig(COMMAND_T* _ct) {
	UpdateMuteOthers(_ct, -1);
}

/////

int IsOfflineOthersLiveConfigEnabled(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		return (lc->m_offlineOthers == 1);
	return false;
}

// _val: 0, 1 or <0 to toggle
// gui refresh not needed (only in ctx menu)
void UpdateOfflineOthers(COMMAND_T* _ct, int _val)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user)) {
		lc->m_offlineOthers = _val<0 ? !lc->m_offlineOthers : _val;
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(_ct), UNDO_STATE_MISCCFG, -1);
		// RefreshToolbar()) not required..
	}
}

void EnableOfflineOthersLiveConfig(COMMAND_T* _ct) {
	UpdateOfflineOthers(_ct, 1);
}

void DisableOfflineOthersLiveConfig(COMMAND_T* _ct) {
	UpdateOfflineOthers(_ct, 0);
}

void ToggleOfflineOthersLiveConfig(COMMAND_T* _ct) {
	UpdateOfflineOthers(_ct, -1);
}

/////

int IsAllNotesOffLiveConfigEnabled(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		return (lc->m_cc123 == 1);
	return false;
}

// _val: 0, 1 or <0 to toggle
// gui refresh not needed (only in ctx menu)
void UpdateAllNotesOff(COMMAND_T* _ct, int _val)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user)) {
		lc->m_cc123 = _val<0 ? !lc->m_cc123 : _val;
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(_ct), UNDO_STATE_MISCCFG, -1);
		// RefreshToolbar()) not required..
	}
}

void EnableAllNotesOffLiveConfig(COMMAND_T* _ct) {
	UpdateAllNotesOff(_ct, 1);
}

void DisableAllNotesOffLiveConfig(COMMAND_T* _ct) {
	UpdateAllNotesOff(_ct, 0);
}

void ToggleAllNotesOffLiveConfig(COMMAND_T* _ct) {
	UpdateAllNotesOff(_ct, -1);
}

/////

int IsTinyFadesLiveConfigEnabled(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		return (lc->m_fade > 0);
	return false;
}

// _val: 0, 1 or <0 to toggle
// gui refresh not needed (only in ctx menu)
void UpdateTinyFadesLiveConfig(COMMAND_T* _ct, int _val)
{
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
	{
		lc->m_fade = _val<0 ? (lc->m_fade>0 ? 0 : DEF_FADE) : _val;
		Undo_OnStateChangeEx2(NULL, SWS_CMD_SHORTNAME(_ct), UNDO_STATE_MISCCFG, -1);

		ScheduledJob::Schedule(new LiveConfigsUpdateFadeJob(lc->m_fade));

		if (LiveConfigsWnd* w = g_lcWndMgr.Get())
			w->Update();
		// RefreshToolbar()) not required..
	}
}

void EnableTinyFadesLiveConfig(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		if (lc->m_fade <= 0)
			UpdateTinyFadesLiveConfig(_ct, DEF_FADE);
}

void DisableTinyFadesLiveConfig(COMMAND_T* _ct) {
	if (LiveConfig* lc = g_liveConfigs.Get()->Get((int)_ct->user))
		if (lc->m_fade > 0)
			UpdateTinyFadesLiveConfig(_ct, 0);
}

void ToggleTinyFadesLiveConfig(COMMAND_T* _ct) {
	UpdateTinyFadesLiveConfig(_ct, -1);
}

