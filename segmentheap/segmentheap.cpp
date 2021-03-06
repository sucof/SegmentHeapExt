/*++

Copyright (c) 2000  Microsoft Corporation

Module Name:

simple.c

--*/

#include "stdafx.h"

#include <ntverp.h>

//
// globals
//
EXT_API_VERSION         ApiVersion = { 1, 0, EXT_API_VERSION_NUMBER64, 0 };
WINDBG_EXTENSION_APIS   ExtensionApis;
ULONG SavedMajorVersion;
ULONG SavedMinorVersion;

int DllInit(
	HANDLE hModule,
	DWORD  dwReason,
	DWORD  dwReserved
)
{
	switch (dwReason) {
	case DLL_THREAD_ATTACH:
		break;

	case DLL_THREAD_DETACH:
		break;

	case DLL_PROCESS_DETACH:
		break;

	case DLL_PROCESS_ATTACH:
		break;
	}

	return TRUE;
}


VOID
WinDbgExtensionDllInit(
	PWINDBG_EXTENSION_APIS lpExtensionApis,
	USHORT MajorVersion,
	USHORT MinorVersion
)
{
	ExtensionApis = *lpExtensionApis;

	SavedMajorVersion = MajorVersion;
	SavedMinorVersion = MinorVersion;

	return;
}

LPEXT_API_VERSION
ExtensionApiVersion(
	VOID
)
{
	//
	// ExtensionApiVersion should return EXT_API_VERSION_NUMBER64 in order for APIs
	// to recognize 64 bit addresses.  KDEXT_64BIT also has to be defined before including
	// wdbgexts.h to get 64 bit headers for WINDBG_EXTENSION_APIS
	//
	return &ApiVersion;
}

//
// Routine called by debugger after load
//
VOID
CheckVersion(
	VOID
)
{
	return;
}


DECLARE_API(heapinfo)
{
	_BucketHeader m_BucketHeader = { 0 };
	ULONG_PTR m_BlockSizeMap;
	ULONG cb;
	WORD m_FreeCount;
	DWORD m_TotalCount;
	DWORD dwBucketIndex;
	DWORD BitmapRealSize = 0;
	ULONG_PTR m_address;
	ULONG_PTR m_bucketheader;
	ULONG_PTR m_bucketmgr;
	ULONG_PTR favalidatebucket, bavalidatebucket;
	BYTE m_StateBitmap[0x800];
	DWORD BucketHeaderSize;
	if (!GetExpression(args)) {
		dprintf("Usage:   !heapinfo <heap address>\n");
		return;
	}
	m_address = GetExpression(args);
	WORD wBucketSizeMap[0x100] = { 0 };
	ReloadSymbols("srv*c:\symbols*http://msdl.microsoft.com/download/symbols");
	m_BlockSizeMap = GetExpression("ntdll!RtlpBucketBlockSizes");
	if (m_BlockSizeMap) {
		ReadMemory(m_BlockSizeMap, wBucketSizeMap, 0x100, &cb);
	}
	else {
		dprintf("Symbol Error, Try after .reload\n");
		return;
	}
	DWORD refcount = 0;
	dprintf("Try to find Bucket Manager.");
	m_bucketheader = m_address & 0xfffffffffffff000;
	DWORD bucket_magic;
	while (1) {	
		if (ReadMemory(m_bucketheader + 0x10, &m_bucketmgr, sizeof(ULONG_PTR), &cb)) {
			if (ReadMemory((m_bucketmgr & 0xfffffff0000) + 0x10, &bucket_magic, sizeof(DWORD), &cb)) {
				if (bucket_magic == 0xddeeddee) {
					break;
				}
			}
		}
		m_bucketheader = m_bucketheader - 0x1000;
		refcount++;
		if (refcount == 0x50) {
			dprintf("\nSearch 0x20 pages, FIND BUCKET HEADER FAILURE...CHECK HEAP ADDRESS...\n");
			return;
		}
		dprintf(".");
	}
	dprintf("\n");
	if (ReadMemory(m_bucketheader, &m_BucketHeader, sizeof(BUCKETHEADER), &cb)) {
		dprintf("Bucket Header:  0x%p\n", m_bucketheader);
		dprintf("Bucket Flink:   0x%p\n", m_BucketHeader.fBucket);
		dprintf("Bucket Blink:   0x%p\n", m_BucketHeader.bBucket);
		dprintf("Bucket Manager: 0x%p\n", m_BucketHeader.BucketManager);
		dprintf("---------------------Bucket Info---------------------\n");
		m_FreeCount = m_BucketHeader.FreeCount;
		m_TotalCount = m_BucketHeader.TotalCount;
		dprintf("Free Heap Count:  %d\n", m_FreeCount);
		dprintf("Total Heap Count: %d\n", m_TotalCount);
		ReadMemory(m_BucketHeader.BucketManager, &dwBucketIndex, sizeof(DWORD), &cb);
		dwBucketIndex = dwBucketIndex / 0x100;
		dprintf("Block Size:       0x%x\n", wBucketSizeMap[dwBucketIndex]);
		dprintf("--Index-- | -----Heap Address----- | --Size-- | --State--\n");	
		for (int i = 0; i < m_TotalCount + 4; i = i+4) {
			m_StateBitmap[i] = m_BucketHeader.bitmap[i / 4] & 0x3;
			m_StateBitmap[i + 1] = (m_BucketHeader.bitmap[i / 4] & 0xc) >> 2;
			m_StateBitmap[i + 2] = (m_BucketHeader.bitmap[i / 4] & 0x30) >> 4;
			m_StateBitmap[i + 3] = (m_BucketHeader.bitmap[i / 4] & 0xc0) >> 6;
		}
		DWORD CommitUnitOffset = (m_BucketHeader.data3 >> 40) & 0xff;
		DWORD dwBitmapSize = (((m_TotalCount / 4 + 0x7) & 0xff8) + CommitUnitOffset * 2 + 0xf) & 0xff0;
		BucketHeaderSize = 0x30 + dwBitmapSize;
		ULONG_PTR m_blockaddress;
		for (int i = 0; i < m_TotalCount; i++) {
			m_blockaddress = m_bucketheader + BucketHeaderSize + wBucketSizeMap[dwBucketIndex] * i;
			if (m_StateBitmap[i] & 0x1) {
				if ((WORD)(m_address - m_blockaddress) < wBucketSizeMap[dwBucketIndex]) {
					dprintf("%04d      | *0x%p    | 0x%04x   | Busy\n", i, m_blockaddress, wBucketSizeMap[dwBucketIndex]);
					dprintf("--------- | ---------------------- | -------- | ---------\n");
				}
				else {
					dprintf("%04d      | 0x%p     | 0x%04x   | Busy\n", i, m_blockaddress, wBucketSizeMap[dwBucketIndex]);
					dprintf("--------- | ---------------------- | -------- | ---------\n");
				}
			}
			else{
				if ((WORD)(m_address - m_blockaddress) < wBucketSizeMap[dwBucketIndex]) {
					dprintf("%04d      | *0x%p    | 0x%04x   | Free\n", i, m_blockaddress, wBucketSizeMap[dwBucketIndex]);
					dprintf("--------- | ---------------------- | -------- | ---------\n");
				}
				else {
					dprintf("%04d      | 0x%p     | 0x%04x   | Free\n", i, m_blockaddress, wBucketSizeMap[dwBucketIndex]);
					dprintf("--------- | ---------------------- | -------- | ---------\n");
				}
			}
		}
	}
	else {
		dprintf("ERROR GET HEAP INFO...CHECK HEAP ADDRESS\n");
	}
	return;
}