"""Canonical v2 electrical data and executable configuration contracts.

Runtime control, pin-override rejection and golden parity belong to the
production-policy host tests, not source-layout or documentation-copy checks.
"""
import html
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HW = "docs/hardware/mimari-v2/"


def text(path):
    source = ROOT / path
    if not source.is_file():
        raise AssertionError("Required current guidance is missing: " + path)
    return source.read_text(encoding="utf-8")


def macros(default=False, example=False):
    config = ROOT / "firmware/fftviz/config.h"
    with tempfile.TemporaryDirectory() as tmp:
        if default or example:
            config = Path(tmp) / "config.h"
            config.write_text(text("firmware/fftviz/config.h"), encoding="utf-8")
        if example:
            (Path(tmp) / "user_config.h").write_text(
                text("firmware/fftviz/user_config.example.h"), encoding="utf-8")
        result = subprocess.run(["c++", "-E", "-dM", "-x", "c++", str(config)],
                                capture_output=True, text=True, check=True)
    return {k: int(v, 0) for k, v in re.findall(
        r"^#define (CFG_\w+) (-?(?:0x[0-9a-fA-F]+|\d+))\s*$",
        result.stdout, re.M)}


def normalized(value):
    return re.sub(r"\s+", " ", html.unescape(value).replace("**", "").replace("`", ""))


class Contract(unittest.TestCase):
    def require(self, source, *patterns):
        value = normalized(source)
        for pattern in patterns:
            self.assertIsNotNone(re.search(pattern, value, re.I), "Missing guidance: " + pattern)

    def reject(self, source, pattern):
        self.assertIsNone(re.search(pattern, normalized(source), re.I), "Stale guidance: " + pattern)


class ConfigurationTests(Contract):
    def test_CTRL_1_default_and_effective_pins_match_canonical_nets(self):
        nets = json.loads(text(HW + "netlist.json"))["nets"]
        roles = {"TFT_SCK": "SCK_E", "TFT_MOSI": "SDA_E", "TFT_DC": "DC_E",
                 "TFT_RST": "RES", "TFT_BLK": "BLK", "MIC": "ADC", "LED": "DATA",
                 "IR": "IR_OUT"}
        for default in (True, False):
            values = macros(default)
            for role, net in roles.items():
                gpio = [int(p[5:]) for p in nets[net]["pinler"] if p.startswith("U1.IO")]
                self.assertEqual(len(gpio), 1, net)
                self.assertEqual(values["CFG_PIN_" + role], gpio[0], (default, role))
            self.assertEqual(values["CFG_PIN_BTN_BOOT"], 0)
            self.assertEqual(values["CFG_ADP_DETECT"], 0)
            self.assertNotIn("CFG_PIN_BTN_MODE", values)

    def test_U6_U7_conservative_example_configuration(self):
        values = macros(example=True)
        for macro, value in (("LED_MA", 60), ("MA_BUDGET", 2500),
                             ("PSU_MA", 3450), ("PSU_PCT", 80)):
            with self.subTest(macro=macro):
                self.assertEqual(values["CFG_" + macro], value)


class NetlistTests(Contract):
    def test_CTRL_1_canonical_electrical_routes(self):
        data = json.loads(text(HW + "netlist.json"))
        nets = data["nets"]
        for pin in ("D2.A", "D4.A", "SW1.2", "J2.3"):
            self.assertIn(pin, nets["V5"]["pinler"])
        self.assertIn("IR1.VCC", nets["V33_DSP"]["pinler"])
        self.assertIn("IR1.GND", nets["DGND"]["pinler"])
        self.assertNotIn("SW2", data["parts"])
        self.assertNotIn("BTN", nets)
        for name, net in nets.items():
            self.assertNotIn("U1.IO4", net["pinler"], name)
            self.assertFalse(any(pin.startswith("SW2.") for pin in net["pinler"]), name)
        self.assertNotIn("U1.IO4", data.get("kullanım", {}))
        self.assertFalse(any("U1.IO35" in n["pinler"] for n in nets.values()))

    def test_HW_1_definitive_values_and_fitted_provenance_classes(self):
        data = json.loads(text(HW + "netlist.json"))
        self.assertEqual(data["meta"]["sürüm"], "2.3")
        parts = data["parts"]
        classes = {
            "Z": "PSU1 J1 J2 STRIP1 U1 U2 D4 D5 C2 R1 DSP1 MIC1 R6 C3",
            "E1": "C1 R2 R3 R4 R5 C6 R7 C4 C5 C7",
            "E2": "F1 D1 D2 FB1 SK1",
            "Ö": "SW1 IR1 ANT1",
        }
        self.assertEqual(set(parts), {ref for refs in classes.values() for ref in refs.split()})
        for category, refs in classes.items():
            for ref in refs.split():
                with self.subTest(ref=ref):
                    part = parts[ref]
                    self.assertEqual(part["sınıf"], category)
                    self.assertNotEqual(part["tip"], "LINK")
                    self.assertIn(part["fit"], ("base", "full"))
                    self.assertEqual(part["elde"], "elde")
                    self.reject(part["değer"], r"köprü|jumper|alternatif|yoksa|ya da")
        values = {
            "R1": "220 Ω", "R2": "10 kΩ", "R3": "47 Ω", "R4": "47 Ω",
            "R5": "47 Ω", "R6": "1 kΩ", "R7": "47 Ω",
            "C1": "1000 µF / 16 V", "C2": "47 nF", "C3": "10 nF",
            "C4": "10 µF / 16 V", "C5": "47 nF", "C6": "10 µF / 16 V",
            "C7": "100 µF / 16 V",
        }
        for ref, value in values.items():
            with self.subTest(ref=ref):
                self.assertEqual(normalized(parts[ref]["değer"]), value)
                self.assertEqual(parts[ref]["tip"], "R" if ref.startswith("R") else
                                 ("C_ELEC" if "µF" in value else "C"))
        for ref in ("D1", "D4", "D5"):
            self.require(parts[ref]["değer"], r"\b1N4007\b")
        self.require(parts["D2"]["değer"], r"\b1N5819\b")
        self.require(parts["U2"]["değer"], r"\bSN74HC00N\b")
        self.require(parts["F1"]["değer"], r"(?:T\s*5\s*A|5\s*A.{0,30}T)", r"5\s*[×x]\s*20")
        self.require(json.dumps(parts["FB1"], ensure_ascii=False),
                     r"(?:axial|eksenel|delikli)", r"ferrit", r"0[,.]5\s*A",
                     r"30.{0,10}600", r"100\s*MHz")
        self.require(json.dumps(parts["SW1"], ensure_ascii=False),
                     r"rocker", r"(?:lamp|lamba).{0,100}(?:NC|yalıt|izole|bağlanma)")

    def test_HW_1_distinct_power_nets_and_HC00_pinout(self):
        data = json.loads(text(HW + "netlist.json"))
        nets = data["nets"]
        expected = {
            "PSU_P": ("PSU1.+5V", "F1.1"),
            "FUSED": ("F1.2", "SW1.1", "D1.K"),
            "V5": ("SW1.2", "D2.A", "D4.A", "J2.3", "C1.+"),
            "ESP5V": ("D2.K", "U1.5V", "C7.+"),
            "GND": ("D1.A", "U1.GND_T5", "C7.-", "J1.3", "J1.4", "STRIP1.GND"),
        }
        for name, pins in expected.items():
            for pin in pins:
                with self.subTest(net=name, pin=pin):
                    self.assertEqual([n for n, net in nets.items() if pin in net["pinler"]], [name])
        for pair in (("D4.K", "D5.A"), ("D5.K", "U2.14")):
            self.assertEqual(sum(all(pin in net["pinler"] for pin in pair)
                                 for net in nets.values()), 1, pair)
        pinout = data["library"]["HC00_DIP14"]["pinler"]
        for pin, name in enumerate(("1A", "1B", "1Y", "2A", "2B", "2Y", "GND",
                                    "3Y", "3A", "3B", "4Y", "4A", "4B", "VCC"), 1):
            self.assertEqual(pinout[str(pin)]["ad"], name)

    def test_HW_4_local_bulk_and_controller_allowance(self):
        data = json.loads(text(HW + "netlist.json"))
        self.assertAlmostEqual(data["meta"]["varsayımlar"]["esp_akim_a"], 0.6)
        self.assertGreaterEqual(data["nets"]["ESP5V"]["imax_a"], 0.6)
        self.require(json.dumps(data["parts"].get("C7", {}), ensure_ascii=False),
                     r"100\s*µF", r"16\s*V", r"(?:U1|D2).{0,100}(?:yakın|near)|(?:yakın|near).{0,100}(?:U1|D2)")
        self.assertIn("U1.GND_B12", data["nets"]["DGND"]["pinler"])
        self.assertIn("U1.GND_B18", data["nets"]["AGND"]["pinler"])
        for name in ("V33_DSP", "V33_MIC"):
            self.assertNotIn("U1.5V", data["nets"][name]["pinler"])
            self.assertNotIn("U1.3V3", data["nets"][name]["pinler"])


if __name__ == "__main__":
    unittest.main()
