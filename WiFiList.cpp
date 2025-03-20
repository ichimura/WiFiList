#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <chrono>
#include <codecvt>
#include <format>
#include <iostream>
#include <locale>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <thread>
#include <vector>
#include <Windows.h>
#include <wlanapi.h>
#include <coguid.h>
#pragma comment(lib, "wlanapi.lib")
using namespace std::literals;

constexpr int channelIds[] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	36, 40, 44, 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
};

constexpr std::wstring_view phyTypes[] = {
	L"unknown"sv,
	L"fhss"sv,
	L"dsss"sv,
	L"irbaseband"sv,
	L"11a"sv,
	L"11b"sv,
	L"11g"sv,
	L"11n"sv,
	L"11ac"sv,
	L"11ad"sv,
	L"11ax"sv,
	L"11be"sv,
};

auto widen(std::string_view str) {
	static std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
	return converter.from_bytes(str.data(), str.data() + str.size());
}

constexpr auto make_unique(auto pointer, auto deleter) {
	return std::unique_ptr<std::remove_pointer_t<decltype(pointer)>, decltype(deleter)>{ pointer, deleter };
}

struct Wlan {
	HANDLE handle;
	DWORD negotiatedVersion;
	Wlan() {
		if (auto result = WlanOpenHandle(WLAN_API_VERSION_2_0, nullptr, &negotiatedVersion, &handle); result != ERROR_SUCCESS)
			throw std::system_error(result, std::system_category(), "WlanOpenHandle failed");
	}
	auto GetInterfaceGuid() {
		WLAN_INTERFACE_INFO_LIST* interfaceList;
		if (auto result = WlanEnumInterfaces(handle, nullptr, &interfaceList); result != ERROR_SUCCESS)
			throw std::system_error(result, std::system_category(), "WlanEnumInterfaces failed");
		auto interfaceListPtr = make_unique(interfaceList, WlanFreeMemory);
		for (auto& interfaceInfo : std::span{ interfaceList->InterfaceInfo, interfaceList->InterfaceInfo + interfaceList->dwNumberOfItems }) {
			//std::wcout << "Interface: " << interfaceInfo.strInterfaceDescription << std::endl;
			if (interfaceInfo.isState == wlan_interface_state_connected)
				return interfaceInfo.InterfaceGuid;
		}
		throw std::runtime_error{ "No connected interface found" };
	}
	void Scan(GUID& interfaceGuid) {
		if (auto result = WlanScan(handle, &interfaceGuid, nullptr, nullptr, nullptr); result != ERROR_SUCCESS)
			throw std::system_error(result, std::system_category(), "WlanScan failed");
	}
	auto GetNetworkBssList(GUID& interfaceGuid) {
		WLAN_BSS_LIST* bssList;
		if (auto result = WlanGetNetworkBssList(handle, &interfaceGuid, nullptr, dot11_BSS_type_any, false, nullptr, &bssList); result != ERROR_SUCCESS)
			throw std::system_error(result, std::system_category(), "WlanGetNetworkBssList failed");
		return make_unique(bssList, WlanFreeMemory);
	}
	~Wlan() {
		WlanCloseHandle(handle, nullptr);
	}
};

int wmain() {
	setlocale(LC_ALL, "");

	Wlan wlan;
	auto interfaceGuid = wlan.GetInterfaceGuid();
	auto simplified = true;
	for (int i = 0;; i++) {
		std::wcout << std::endl;
		if (i % 6 == 0)
			wlan.Scan(interfaceGuid);
		auto bssList = wlan.GetNetworkBssList(interfaceGuid);
		std::map<uint64_t, std::vector<std::wstring>> ssids;
		for (auto& entry : std::span{ bssList->wlanBssEntries, bssList->wlanBssEntries + bssList->dwNumberOfItems }) {
			auto& bssid = entry.dot11Bssid;
			auto id = simplified ? (uint64_t)bssid[1] << 24 | (uint64_t)bssid[2] << 16 | (uint64_t)bssid[3] << 8 | bssid[4] : (uint64_t)bssid[0] << 40 | (uint64_t)bssid[1] << 32 | (uint64_t)bssid[2] << 24 | (uint64_t)bssid[3] << 16 | (uint64_t)bssid[4] << 8 | bssid[5];
			auto ssid = 0 < entry.dot11Ssid.uSSIDLength ? widen({ reinterpret_cast<const char*>(entry.dot11Ssid.ucSSID), entry.dot11Ssid.uSSIDLength }) : std::format(L"{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}"sv, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
			ssids[id].push_back(ssid);
			//std::wcout << std::format(L"SSID: {}, Signal quality: {}, PHY type: {}."sv, ssid, entry.uLinkQuality, phyTypes[entry.dot11BssPhyType]) << std::endl;
		}
		auto signalQuality = channelIds | std::ranges::views::transform([s = ssids.size()](int k) { return std::pair{ k, std::vector<int>(s) }; }) | std::ranges::to<std::map>();
		for (auto& entry : std::span{ bssList->wlanBssEntries, bssList->wlanBssEntries + bssList->dwNumberOfItems }) {
			auto& bssid = entry.dot11Bssid;
			auto id = simplified ? (uint64_t)bssid[1] << 24 | (uint64_t)bssid[2] << 16 | (uint64_t)bssid[3] << 8 | bssid[4] : (uint64_t)bssid[0] << 40 | (uint64_t)bssid[1] << 32 | (uint64_t)bssid[2] << 24 | (uint64_t)bssid[3] << 16 | (uint64_t)bssid[4] << 8 | bssid[5];
			auto index = std::distance(ssids.begin(), ssids.find(id));
			std::set<int> channels;
			for (auto ie = std::span{ reinterpret_cast<const unsigned char*>(&entry) + entry.ulIeOffset, entry.ulIeSize }; 2 <= ie.size(); ie = ie.subspan(2 + ie[1])) {
				if (ie[0] == 61 && ie[1] == 22) {					// 9.4.2.56 HT Operation element, Wi-Fi 4
					auto primary = ie[2];
					auto secondary = (ie[3] & 0b11) == 1 ? primary + 4 : (ie[3] & 0b11) == 3 ? primary - 4 : 0;
					auto channelWidth = ie[3] & 0b100;
					if (channelWidth == 0) {
						//std::wcout << std::format(L"  HT Operation: {}ch for 20MHz."sv, primary) << std::endl;
						channels.insert({ primary });
					} else {
						//std::wcout << std::format(L"  HT Operation: {}ch+{}ch for 40MHz, {}ch for 20MHz."sv, primary, secondary, primary) << std::endl;
						channels.insert({ primary, secondary });
					}
				}
				if (ie[0] == 192 && ie[1] == 5) {					// 9.4.2.158 VHT Operation element, Wi-Fi 5
					auto channelWidth = ie[2];
					auto primary = ie[3];
					auto secondary = ie[4];
					if (channelWidth == 0) {
						//std::wcout << std::format(L"  VHT Operation: see HT."sv) << std::endl;
					} else if (channelWidth == 2) {
						//std::wcout << std::format(L"  VHT Operation: {}ch～{}ch for 160MHz."sv, primary - 14, primary + 14) << std::endl;
						channels.insert({ primary - 14, primary - 10, primary - 6, primary - 2, primary + 2, primary + 6, primary + 10, primary + 14 });
					} else if (channelWidth == 3) {
						//std::wcout << std::format(L"  VHT Operation: {}ch～{}ch+{}ch～{}ch for 80+80MHz."sv, primary - 6, primary + 6, secondary - 6, secondary + 6) << std::endl;
						channels.insert({ primary - 6, primary - 2, primary + 2, primary + 6, secondary - 6, secondary - 2, secondary + 2, secondary + 6 });
					} else if (secondary == 0) {
						//std::wcout << std::format(L"  VHT Operation: {}ch～{}ch for 80MHz."sv, primary - 6, primary + 6) << std::endl;
						channels.insert({ primary - 6, primary - 2, primary + 2, primary + 6 });
					} else if (primary + 8 == secondary || primary - 8 == secondary) {
						//std::wcout << std::format(L"  VHT Operation: Channel: {}ch～{}ch for 160MHz, {}ch～{}ch for 80MHz."sv, secondary - 14, secondary + 14, primary - 6, primary + 6) << std::endl;
						channels.insert({ secondary - 14, secondary - 10, secondary - 6, secondary - 2, secondary + 2, secondary + 6, secondary + 10, secondary + 14 });
					} else {
						//std::wcout << std::format(L"  VHT Operation: {}ch～{}ch+{}ch～{}ch for 80+80MHz, {}ch～{}ch for 80MHz."sv, primary - 6, primary + 6, secondary - 6, secondary + 6, primary - 6, primary + 6) << std::endl;
						channels.insert({ primary - 6, primary - 2, primary + 2, primary + 6, secondary - 6, secondary - 2, secondary + 2, secondary + 6 });
					}
				}
				if (ie[0] == 255 && 7 <= ie[1] && ie[2] == 36) {		// 9.4.2.249 HE Operation element, Wi-Fi 6E
					auto hasVHT = (ie[4] & 0b1000000) != 0;
					auto hasBSS = (ie[4] & 0b10000000) != 0;
					auto has6GHz = (ie[5] & 0b10) != 0;
					//if (hasVHT || has6GHz)
					//	std::wcout << std::format(L"  HE Operation: {}, {}, {}, len = {}.", hasVHT, hasBSS, has6GHz, ie[1]) << std::endl;
				}
			}
			for (auto channel : channels) {
				signalQuality[channel][index] = entry.uLinkQuality;
				if (channel <= 14) {
					if (1 <= channel - 1)
						signalQuality[channel - 1][index] = entry.uLinkQuality;
					if (1 <= channel - 2)
						signalQuality[channel - 2][index] = entry.uLinkQuality;
					if (channel + 1 <= 13)
						signalQuality[channel + 1][index] = entry.uLinkQuality;
					if (channel + 2 <= 13)
						signalQuality[channel + 2][index] = entry.uLinkQuality;
				}
			}
		}
		std::wcout << L"SSID:"sv << std::endl;
		for (auto [index, pair] : ssids | std::ranges::views::enumerate) {
			auto [id, ssids] = pair;
			std::wcout << std::format(L"   {:2}: {:0{}X}: {:n:s}"sv, index, id, (simplified ? 8 : 12), ssids) << std::endl;
		}
		std::wcout << L"Channel:"sv << std::endl;
		std::wcout << L"      "sv;
		for (auto index : std::ranges::views::iota(0u, ssids.size()))
			std::wcout << std::format(L" {:2}"sv, index);
		std::wcout << std::endl;
		for (auto [channel, qualities] : signalQuality) {
			std::wcout << std::format(L"  {:3}:"sv, channel);
			for (auto quality : qualities)
				std::wcout << (quality == 0 ? L"   "s : std::format(L"{:3}"sv, quality));
			std::wcout << std::endl;
		}
		std::this_thread::sleep_for(5s);
	}
	return 0;
}
