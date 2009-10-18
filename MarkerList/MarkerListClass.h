/******************************************************************************
/ MarkerListClass.h
/
/ Copyright (c) 2009 Tim Payne (SWS)
/ http://www.standingwaterstudios.com/reaper
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

#pragma once

class MarkerItem
{
public:
	MarkerItem(bool bReg, double dPos, double dRegEnd, const char* cName, int id);
	MarkerItem(LineParser* lp);
	~MarkerItem();
	double m_dPos;
	bool m_bReg;
	double m_dRegEnd;
	int m_id;
	char* ItemString(char* str, int iSize);
	void SetName(const char* newname);
	const char* GetName() { return m_cName ? m_cName : ""; }
	void SetFromString(LineParser* lp);
	bool Compare(bool bReg, double dPos, double dRegEnd, const char* cName, int id);
	bool Compare(MarkerItem* mi);
private:
	char* m_cName;
};

class MarkerList
{
public:
	MarkerList(const char* name, bool bGetCurList);
	~MarkerList();
	bool BuildFromReaper();
	void UpdateReaper();
#ifdef _WIN32
	void ListToClipboard();
	void ClipboardToList();
	void ExportToClipboard(const char* format);
#endif
	int ApproxSize();
	void CropToTimeSel(bool bOffset);

	char* m_name;
	WDL_PtrList<MarkerItem> m_items;
	HANDLE m_hLock;
};
