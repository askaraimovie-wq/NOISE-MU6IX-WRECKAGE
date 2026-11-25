# NOISE MU6IX — WRECKAGE (v1.0.0)

Cinematic Ensemble Percussion & Modern Drum VST3 Engine (`WRECKAGE.vst3` + `WRECKAGE Player.vst3`) built by **Noise Mu6ix Studio**.

## GitHub-ல் அப்லோட் செய்து Windows `.vst3` Build எடுப்பது எப்படி? (3 படிகள்)

1. உங்கள் GitHub கணக்கில் ஒரு புதிய Repository (எ.கா: `NOISE-MU6IX-WRECKAGE`) உருவாக்கவும்.
2. இந்த ZIP-க்குள் உள்ள அனைத்து கோப்புகளையும் (`.github/workflows/build.yml` உட்பட!) அந்த Repository-இன் `main` பிராஞ்சில் அப்லோட் (Push / Upload) செய்யவும்.
3. GitHub-இன் **Actions** டேப்பைத் திறக்கவும் — சுமார் 3–4 நிமிடங்களில் பச்சை நிற ✔ வந்தவுடன், கீழே உள்ள **Artifacts** பகுதியில் **`NOISE_MU6IX_WRECKAGE_v1.0.0_Windows_x64.zip`** கோப்பைப் பதிவிறக்கவும்!

### FL Studio-வில் நிறுவல் (Windows)
1. பதிவிறக்கிய ZIP-ஐ Unzip செய்து, உள்ளே உள்ள **`WRECKAGE.vst3`** மற்றும் **`WRECKAGE Player.vst3`** இரண்டையும் `C:\Program Files\Common Files\VST3\` ஃபோல்டரில் நகலெடுக்கவும்.
2. FL Studio → *Options → Manage Plugins → Find installed plugins* கிளிக் செய்யவும்.
3. FL Studio-வில் **`WRECKAGE`** மற்றும் **`WRECKAGE Player`** என இரண்டும் தனித்தனியாகத் தோன்றும்!

### வெர்ஷன் எண் உயர்த்தும் விதி (Strict Base-10 Rollover)
புதிய வெர்ஷன் வெளியிட:
```bash
python scripts/bump_version.py
```
(`1.0.0` → `1.0.9` → `1.1.0` → ... → `1.9.9` → `2.0.0`)
