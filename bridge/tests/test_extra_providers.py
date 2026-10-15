"""Gemini and Grok support: parsing, capability gating and tray text."""

from __future__ import annotations

import json
import unittest
from datetime import datetime, timezone

from quotaframe_bridge.domain.models import (
    EXTRA_PROVIDERS_CAPABILITY,
    Provider,
    ProviderUsage,
    SourceState,
    UsageWindow,
)
from quotaframe_bridge.service.multi_device import DeviceSession, UsageSnapshot
from quotaframe_bridge.sources.codexbar_mac import parse_codexbar_mac_json
from quotaframe_bridge.ui.status import PanelStatus, format_info_lines

from test_multi_device_service import RecordingTransport

UPSTREAM = json.dumps([
    {
        "provider": "gemini",
        "usage": {
            "primary": {"usedPercent": 12.5, "resetsAt": "2026-10-16T00:00:00Z"},
            "secondary": {"usedPercent": 3, "resetsAt": "2026-10-16T00:00:00Z"},
            "updatedAt": "2026-10-15T14:00:00Z",
        },
    },
    {
        "provider": "grok",
        "usage": {
            "primary": {"usedPercent": 140.2, "resetsAt": "2026-10-20T00:00:00Z"},
            "secondary": None,
            "updatedAt": "2026-10-15T14:00:00Z",
        },
    },
    {"provider": "cursor", "usage": {"primary": {"usedPercent": 1}}},
])


def usage(provider: Provider, percent: int) -> ProviderUsage:
    return ProviderUsage(provider, SourceState.PARTIAL, 1000, short=UsageWindow(percent))


class ExtraProviderParserTests(unittest.TestCase):
    def test_gemini_and_grok_are_parsed_and_unrelated_providers_ignored(self) -> None:
        parsed = parse_codexbar_mac_json(UPSTREAM, attempted_at=5)

        gemini = parsed[Provider.GEMINI]
        self.assertIs(gemini.state, SourceState.OK)
        self.assertEqual(gemini.short.used_percent, 13)
        self.assertEqual(gemini.week.used_percent, 3)

        grok = parsed[Provider.GROK]
        self.assertIs(grok.state, SourceState.PARTIAL)
        self.assertEqual(grok.short.used_percent, 100)
        self.assertIsNone(grok.week)

        for provider in (Provider.CODEX, Provider.CLAUDE):
            self.assertIs(parsed[provider].state, SourceState.UNAVAILABLE)


class CapabilityGatingTests(unittest.IsolatedAsyncioTestCase):
    async def publish(self, capabilities: frozenset[str]) -> list[str]:
        transport = RecordingTransport(capabilities=capabilities)
        session = DeviceSession(
            "panel", transport,
            wall_now=lambda: datetime.fromtimestamp(1000, timezone.utc))
        snapshot = UsageSnapshot(
            codex=usage(Provider.CODEX, 1),
            claude=usage(Provider.CLAUDE, 2),
            gemini=usage(Provider.GEMINI, 3),
            grok=usage(Provider.GROK, 4),
        )
        try:
            await session._publish(snapshot)
        finally:
            await session.close()
        return [json.loads(data)["provider"] for data, command, _ in transport.sent
                if command == "usage"]

    async def test_stock_firmware_only_receives_codex_and_claude(self) -> None:
        self.assertEqual(await self.publish(frozenset({"usage.v1"})), ["codex", "claude"])

    async def test_capable_firmware_receives_all_four(self) -> None:
        sent = await self.publish(frozenset({"usage.v1", EXTRA_PROVIDERS_CAPABILITY}))
        self.assertEqual(sent, ["codex", "claude", "gemini", "grok"])


class TrayTextTests(unittest.TestCase):
    def test_extra_providers_appear_only_with_data(self) -> None:
        providers = {
            Provider.CODEX: usage(Provider.CODEX, 10),
            Provider.CLAUDE: usage(Provider.CLAUDE, 20),
            Provider.GEMINI: ProviderUsage(Provider.GEMINI, SourceState.UNAVAILABLE, 1),
            Provider.GROK: usage(Provider.GROK, 30),
        }
        status = PanelStatus(devices=(), providers=providers,
                             consecutive_failures=0, resolution_error=None)
        _, text = format_info_lines(status)
        self.assertIn("Grok", text)
        self.assertNotIn("Gemini", text)


if __name__ == "__main__":
    unittest.main()
