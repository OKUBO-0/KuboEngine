@echo off
setlocal
cd /d "%~dp0\..\.."

set "FONT=%~1"
if "%FONT%"=="" set "FONT=C:\Windows\Fonts\NotoSansJP-VF.ttf"
set "PYTHON=python"
python -c "import PIL" >nul 2>nul
if errorlevel 1 (
  set "PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
  if not exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" (
    echo Python with Pillow was not found. Install it with: python -m pip install Pillow
    exit /b 1
  )
)

"%PYTHON%" tools\bitmap_font\convert_bitmap_font.py ^
  --font "%FONT%" ^
  --size 72 ^
  --variation Black ^
  --padding 4 ^
  --atlas-width 2048 ^
  --scan game\directxgame\scene\GameTitleScene.cpp ^
  --scan game\directxgame\ui\hud\LevelUpSelectionHud.cpp ^
  --scan game\directxgame\core\LevelUpChoiceService.cpp ^
  --scan game\directxgame\player\PassiveItemData.h ^
  --output-png Resources\DirectXGame\ui\font\noto_sans_jp_black.png ^
  --output-json Resources\DirectXGame\ui\font\noto_sans_jp_black.json

if errorlevel 1 exit /b %errorlevel%
echo Bitmap font generation completed.
