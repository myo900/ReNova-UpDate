# tools/publish_fw.py
#
# Post-build per PlatformIO:
#  - si aggancia all'artefatto $BUILD_DIR/${PROGNAME}.bin
#  - al termine della build: trova il .bin, copia nel repo OTA,
#    aggiorna manifest.json / latest.json e fa git add/commit/push.
#
# platformio.ini (nell'env che compili):
#   extra_scripts = post:tools/publish_fw.py
#   custom_publish_repo   = C:/Work/ReNova-UpDate
#   custom_publish_branch = main
#   custom_manifest_name  = manifest.json
#   custom_latest_name    = latest.json
#   custom_repo_user      = myo900
#   custom_repo_name      = ReNova-UpDate
#   ; opzionale se vuoi suffix per beta: custom_fw_suffix = -beta

import os, shutil, subprocess, json, hashlib, re
from datetime import datetime
from pathlib import Path
from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

def get_opt(name, default=""):
    try:
        return env.GetProjectOption(name)
    except Exception:
        return os.environ.get(name.upper(), default)

REPO_DIR       = Path(get_opt("custom_publish_repo", "")).expanduser()
BRANCH         = get_opt("custom_publish_branch", "main")
MANIFEST_NAME  = get_opt("custom_manifest_name", "manifest.json")
LATEST_NAME    = get_opt("custom_latest_name", "latest.json")
REPO_USER      = get_opt("custom_repo_user", "myo900")
REPO_NAME      = get_opt("custom_repo_name", "ReNova-UpDate")
FW_SUFFIX      = get_opt("custom_fw_suffix", "")  # es. "-beta" per i binari beta

# ---- callback post-build ----
def _publish_action(target, source, env):
    # target/source sono liste di nodi SCons; env è l'env di build
    if not REPO_DIR:
        print("[publish_fw] ⚠️ custom_publish_repo non configurato. Skip.")
        return

    # Directory progetto/build ottenute dall'env passato da SCons
    PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
    BUILD_DIR   = Path(env.subst("$BUILD_DIR"))
    PROGNAME    = env.subst("$PROGNAME") or "firmware"

    # 1) individua il .bin (prima prova col nome ufficiale, poi il più recente)
    fw_candidate = BUILD_DIR / f"{PROGNAME}.bin"
    FW_SRC = None
    if fw_candidate.exists():
        FW_SRC = fw_candidate
    else:
        bins = sorted(BUILD_DIR.glob("*.bin"), key=lambda p: p.stat().st_mtime, reverse=True)
        if bins:
            FW_SRC = bins[0]

    if not FW_SRC or not FW_SRC.exists():
        print("[publish_fw] ⚠️ Nessun file .bin trovato in:", BUILD_DIR)
        for p in BUILD_DIR.glob("*"):
            print("   -", p.name)
        return

    print(f"[publish_fw] Userò questo firmware: {FW_SRC.name}")

    # 2) estrai versione da include/web_config.h
    WEB_CONFIG = PROJECT_DIR / "include" / "web_config.h"
    def extract_version_from_header(header: Path) -> str:
        if not header.exists():
            return "0.0.0"
        txt = header.read_text(encoding="utf-8", errors="ignore")
        m = re.search(r'#\s*define\s+FW_VERSION\s+"([^"]+)"', txt)
        return m.group(1).strip() if m else "0.0.0"

    VERSION = extract_version_from_header(WEB_CONFIG) or "0.0.0"

    # 3) prepara nomi/URL
    FW_NAME = f"firmware-{VERSION}{FW_SUFFIX}.bin"
    FW_DST  = REPO_DIR / FW_NAME
    MANIFEST = REPO_DIR / MANIFEST_NAME
    LATEST   = REPO_DIR / LATEST_NAME
    # URL con percorso firmware/ se REPO_DIR è relativo
    fw_path = f"firmware/{FW_NAME}" if not REPO_DIR.is_absolute() else FW_NAME
    RAW_URL  = f"https://raw.githubusercontent.com/{REPO_USER}/{REPO_NAME}/{BRANCH}/{fw_path}"

    # 4) copia .bin e calcola metadati
    REPO_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copy2(str(FW_SRC), str(FW_DST))

    def sha256_hex(path: Path) -> str:
        h = hashlib.sha256()
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1024*1024), b""):
                h.update(chunk)
        return h.hexdigest().upper()

    def byte_size(path: Path) -> int:
        return path.stat().st_size

    size = byte_size(FW_DST)
    sha  = sha256_hex(FW_DST)
    print(f"[publish_fw] Copiato: {FW_DST.name} ({size} bytes)")
    print(f"[publish_fw] SHA256:  {sha}")

    # 5) genera manifest.json e latest.json
    manifest_data = {
        "version": VERSION,
        "url": RAW_URL,
        "size": size,
        "sha256": sha,
        "released_at": datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    }
    if MANIFEST.exists():
        try:
            old = json.loads(MANIFEST.read_text(encoding="utf-8"))
            if isinstance(old, dict) and "notes" in old and "notes" not in manifest_data:
                manifest_data["notes"] = old["notes"]
        except Exception:
            pass

    for p in (MANIFEST, LATEST):
        p.write_text(json.dumps(manifest_data, indent=2), encoding="utf-8")
        print(f"[publish_fw] Aggiornato {p.name}")

    # 6) git add/commit/push
    def run_cmd(args, cwd):
        res = subprocess.run(args, cwd=str(cwd), capture_output=True, text=True)
        if res.returncode != 0:
            print(f"[publish_fw] ERRORE: {' '.join(args)}")
            print(res.stdout.strip())
            print(res.stderr.strip())
        return res.returncode == 0

    run_cmd(["git", "checkout", BRANCH], REPO_DIR)  # ok anche se già sul branch

    ok = run_cmd(["git", "add", FW_NAME, MANIFEST_NAME, LATEST_NAME], REPO_DIR)
    msg = f"publish {FW_NAME} (+ {MANIFEST_NAME}/{LATEST_NAME})"
    ok = ok and run_cmd(["git", "commit", "-m", msg], REPO_DIR)
    ok = ok and run_cmd(["git", "push", "origin", BRANCH], REPO_DIR)

    if ok:
        print("[publish_fw] ✅ Pubblicazione completata.")
        print(f"[publish_fw] URL bin:      {RAW_URL}")
        print(f"[publish_fw] URL manifest: https://raw.githubusercontent.com/{REPO_USER}/{REPO_NAME}/{BRANCH}/{MANIFEST_NAME}")
    else:
        print("[publish_fw] ⚠️ Pubblicazione NON completata (git). Controlla credenziali o conflitti.")

# ---- registra l'azione post-build sul .bin ufficiale + fallback su buildprog ----
# env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", _publish_action)
env.AddPostAction("buildprog", _publish_action)
