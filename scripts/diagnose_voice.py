#!/usr/init/env python3
"""
scripts/diagnose_voice.py - Diagnostic script for Windows SAPI and offline speech voices.
"""

from __future__ import annotations

import base64
import subprocess
import sys


def run_diagnostic() -> None:
    print("=" * 60)
    print("MudraGuru — Voice & Windows SAPI Diagnostic")
    print("=" * 60)

    # 1. List installed voices via PowerShell SAPI
    ps_list_script = (
        "Add-Type -AssemblyName System.Speech; "
        "$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$voices = $synth.GetInstalledVoices(); "
        "foreach ($v in $voices) { "
        "  $info = $v.VoiceInfo; "
        "  Write-Output ('Voice: {0} | Culture: {1} | Gender: {2}' -f $info.Name, $info.Culture.Name, $info.Gender) "
        "}"
    )
    encoded_list = base64.b64encode(ps_list_script.encode("utf-16le")).decode("ascii")

    print("\n[1] Checking installed Windows Speech Synthesizer voices...")
    try:
        res = subprocess.run(
            ["powershell.exe", "-NoProfile", "-EncodedCommand", encoded_list],
            capture_output=True,
            text=True,
            timeout=10,
        )
        if res.returncode == 0 and res.stdout.strip():
            print(res.stdout.strip())
        else:
            print("No voices reported or command returned non-zero exit code.")
            if res.stderr:
                print("Stderr:", res.stderr.strip())
    except Exception as e:
        print(f"Failed to query SAPI voices: {e}")

    # 2. Test English speech
    print("\n[2] Testing English SAPI speech synthesis...")
    ps_test_en = (
        "Add-Type -AssemblyName System.Speech; "
        "$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$synth.Volume = 100; "
        "$synth.Speak('MudraGuru English voice test successful.');"
    )
    encoded_en = base64.b64encode(ps_test_en.encode("utf-16le")).decode("ascii")
    try:
        res_en = subprocess.run(
            ["powershell.exe", "-NoProfile", "-EncodedCommand", encoded_en],
            capture_output=True,
            text=True,
            timeout=15,
        )
        if res_en.returncode == 0:
            print("English SAPI speech test PASSED.")
        else:
            print(f"English SAPI speech test failed with exit code {res_en.returncode}: {res_en.stderr.strip()}")
    except Exception as e:
        print(f"English speech test error: {e}")

    # 3. Test Telugu speech (if available)
    print("\n[3] Testing Telugu SAPI speech synthesis (if installed)...")
    ps_test_te = (
        "Add-Type -AssemblyName System.Speech; "
        "$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
        "$synth.Volume = 100; "
        "$v = $synth.GetInstalledVoices() | Where-Object { $_.VoiceInfo.Culture.Name -like 'te*' } | Select-Object -First 1; "
        "if ($v) { "
        "  $synth.SelectVoice($v.VoiceInfo.Name); "
        "  $synth.Speak('MudraGuru Telugu voice test successful.'); "
        "  Write-Output 'TELUGU_VOICE_FOUND'; "
        "} else { "
        "  Write-Output 'TELUGU_VOICE_NOT_FOUND'; "
        "}"
    )
    encoded_te = base64.b64encode(ps_test_te.encode("utf-16le")).decode("ascii")
    try:
        res_te = subprocess.run(
            ["powershell.exe", "-NoProfile", "-EncodedCommand", encoded_te],
            capture_output=True,
            text=True,
            timeout=15,
        )
        out_str = res_te.stdout.strip()
        print("Telugu test output:", out_str)
        if "TELUGU_VOICE_FOUND" in out_str:
            print("Telugu SAPI voice is installed and operational.")
        else:
            print("Telugu SAPI voice is NOT installed on this machine. Using English fallback as designed.")
    except Exception as e:
        print(f"Telugu speech test error: {e}")

    print("\n" + "=" * 60)
    print("Voice diagnostic complete.")
    print("=" * 60)


if __name__ == "__main__":
    run_diagnostic()
