from __future__ import annotations

from math import atan2, cos, sin
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "output" / "pdf"
OUT_PDF = OUT_DIR / "program_explanation_A4_rebased_2026.pdf"

PAGE_W, PAGE_H = landscape(A4)
MARGIN_X = 42
TOP_Y = PAGE_H - 34
CONTENT_W = PAGE_W - MARGIN_X * 2

FONT_REG = "BizUDGothicRegular"
FONT_BOLD = "BizUDGothicBold"
FONT_MONO = "Courier"

NAVY = colors.HexColor("#0B1220")
INK = colors.HexColor("#111827")
MUTED = colors.HexColor("#475569")
LINE = colors.HexColor("#CBD5E1")
PAPER = colors.HexColor("#F8FAFC")
BLUE = colors.HexColor("#2563EB")
CYAN = colors.HexColor("#06B6D4")
GREEN = colors.HexColor("#10B981")
ORANGE = colors.HexColor("#F59E0B")
RED = colors.HexColor("#EF4444")
PURPLE = colors.HexColor("#7C3AED")
TEAL = colors.HexColor("#0F766E")


def register_fonts() -> None:
    pdfmetrics.registerFont(TTFont(FONT_REG, r"C:\Windows\Fonts\BIZ-UDGothicR.ttc"))
    pdfmetrics.registerFont(TTFont(FONT_BOLD, r"C:\Windows\Fonts\BIZ-UDGothicB.ttc"))


def sw(value: str, size: float, font: str = FONT_REG) -> float:
    return pdfmetrics.stringWidth(value, font, size)


def fit_lines(value: str, max_w: float, size: float, font: str = FONT_REG) -> list[str]:
    lines: list[str] = []
    for raw in str(value).splitlines():
        current = ""
        for ch in raw.strip():
            test = current + ch
            if not current or sw(test, size, font) <= max_w:
                current = test
            else:
                lines.append(current)
                current = ch
        if current:
            lines.append(current)
        elif raw == "":
            lines.append("")
    return lines


def text(c: canvas.Canvas, x: float, y: float, value: str, size: float = 10, color=INK, font: str = FONT_REG) -> None:
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawString(x, y, value)


def center_text(c: canvas.Canvas, x: float, y: float, value: str, size: float = 10, color=INK, font: str = FONT_REG) -> None:
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawCentredString(x, y, value)


def right_text(c: canvas.Canvas, x: float, y: float, value: str, size: float = 10, color=INK, font: str = FONT_REG) -> None:
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawRightString(x, y, value)


def paragraph(c: canvas.Canvas, x: float, y: float, value: str, max_w: float, size: float = 8.8, leading: float = 12.5, color=INK, font: str = FONT_REG) -> float:
    for line in fit_lines(value, max_w, size, font):
        text(c, x, y, line, size, color, font)
        y -= leading
    return y


def title(c: canvas.Canvas, no: str, label: str, main: str, sub: str) -> float:
    text(c, MARGIN_X, TOP_Y, f"{no} / {label}", 8.2, BLUE, FONT_BOLD)
    text(c, MARGIN_X, TOP_Y - 28, main, 24, NAVY, FONT_BOLD)
    y = paragraph(c, MARGIN_X, TOP_Y - 50, sub, CONTENT_W, 9.2, 12.5, MUTED)
    c.setStrokeColor(LINE)
    c.line(MARGIN_X, y - 7, PAGE_W - MARGIN_X, y - 7)
    return y - 28


def footer(c: canvas.Canvas, page: int) -> None:
    c.setStrokeColor(LINE)
    c.setLineWidth(1)
    c.line(MARGIN_X, 26, PAGE_W - MARGIN_X, 26)
    text(c, MARGIN_X, 13, "KuboEngine / Program Explanation", 7, MUTED)
    right_text(c, PAGE_W - MARGIN_X, 13, f"{page:02d}", 7, MUTED)


def chip(c: canvas.Canvas, x: float, y: float, label: str, fill) -> float:
    size = 8
    w = sw(label, size, FONT_BOLD) + 16
    c.setFillColor(fill)
    c.roundRect(x, y - 12, w, 18, 5, stroke=0, fill=1)
    text(c, x + 8, y - 7, label, size, colors.white, FONT_BOLD)
    return x + w + 6


def box(c: canvas.Canvas, x: float, y: float, w: float, h: float, head: str, body: str = "", accent=BLUE, fill=colors.white, body_size: float = 8.0) -> None:
    c.setFillColor(fill)
    c.setStrokeColor(LINE)
    c.roundRect(x, y - h, w, h, 7, stroke=1, fill=1)
    c.setFillColor(accent)
    c.roundRect(x, y - 5, w, 5, 3, stroke=0, fill=1)
    text(c, x + 12, y - 23, head, 10.8, INK, FONT_BOLD)
    if body:
        paragraph(c, x + 12, y - 40, body, w - 24, body_size, body_size + 3.8, INK)


def bullets(c: canvas.Canvas, x: float, y: float, items: list[str], max_w: float, size: float = 8.4, leading: float = 12.0, color=BLUE) -> float:
    for item in items:
        c.setFillColor(color)
        c.circle(x + 3, y + 3, 2, stroke=0, fill=1)
        for line in fit_lines(item, max_w - 17, size):
            text(c, x + 15, y, line, size, INK)
            y -= leading
        y -= 2
    return y


def node(c: canvas.Canvas, x: float, y: float, w: float, h: float, label: str, fill) -> tuple[float, float, float, float]:
    c.setFillColor(fill)
    c.roundRect(x, y - h, w, h, 6, stroke=0, fill=1)
    lines = fit_lines(label, w - 12, 8.0, FONT_BOLD)
    yy = y - h / 2 + (len(lines) - 1) * 5
    for line in lines:
        center_text(c, x + w / 2, yy, line, 8.0, colors.white, FONT_BOLD)
        yy -= 10
    return x, y, w, h


def arrow(c: canvas.Canvas, x1: float, y1: float, x2: float, y2: float, color=LINE) -> None:
    c.setStrokeColor(color)
    c.setLineWidth(1.35)
    c.line(x1, y1, x2, y2)
    angle = atan2(y2 - y1, x2 - x1)
    head = 7
    spread = 0.45
    p1 = (x2 - head * cos(angle - spread), y2 - head * sin(angle - spread))
    p2 = (x2 - head * cos(angle + spread), y2 - head * sin(angle + spread))
    c.line(x2, y2, p1[0], p1[1])
    c.line(x2, y2, p2[0], p2[1])


def code_block(c: canvas.Canvas, x: float, y: float, w: float, lines: list[str], size: float = 7.2) -> float:
    leading = size + 3.8
    pad_x = 12
    wrapped: list[str] = []
    max_w = w - pad_x * 2
    for line in lines:
        if not line:
            wrapped.append("")
            continue
        while sw(line, size, FONT_MONO) > max_w:
            cut = max(14, int(max_w / sw("M", size, FONT_MONO)))
            wrapped.append(line[:cut])
            line = "  " + line[cut:]
        wrapped.append(line)
    h = 20 + len(wrapped) * leading
    c.setFillColor(colors.HexColor("#0F172A"))
    c.roundRect(x, y - h, w, h, 7, stroke=0, fill=1)
    yy = y - 15
    for line in wrapped:
        text(c, x + pad_x, yy, line, size, colors.HexColor("#F8FAFC"), FONT_MONO)
        yy -= leading
    return y - h


def placeholder(c: canvas.Canvas, x: float, y: float, w: float, h: float, label: str) -> None:
    c.setFillColor(PAPER)
    c.setStrokeColor(LINE)
    c.roundRect(x, y - h, w, h, 7, stroke=1, fill=1)
    c.setStrokeColor(colors.HexColor("#94A3B8"))
    c.setDash(4, 3)
    c.rect(x + 10, y - h + 10, w - 20, h - 20, stroke=1, fill=0)
    c.setDash()
    center_text(c, x + w / 2, y - h / 2 + 4, label, 9, MUTED, FONT_BOLD)


def page_cover(c: canvas.Canvas) -> None:
    c.setFillColor(NAVY)
    c.rect(0, 0, PAGE_W, PAGE_H, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#101A2D"))
    c.circle(PAGE_W - 72, PAGE_H - 42, 130, stroke=0, fill=1)
    c.setFillColor(TEAL)
    c.circle(PAGE_W - 28, 66, 88, stroke=0, fill=1)
    text(c, 54, PAGE_H - 75, "PROGRAMMER // 2026", 10, CYAN, FONT_BOLD)
    text(c, 54, PAGE_H - 143, "プログラム説明資料", 36, colors.white, FONT_BOLD)
    text(c, 56, PAGE_H - 180, "KuboEngine / Octopus", 18, colors.white, FONT_BOLD)
    paragraph(c, 56, PAGE_H - 215, "DirectX 12 × C++20。scene設計、CSVデータ駆動、OBB衝突、DX12リソース寿命管理を中心に構成。", 610, 10.6, 16, colors.HexColor("#F8FAFC"))
    x = 56
    for label, col in [("C++20", BLUE), ("DirectX 12", CYAN), ("HLSL", GREEN), ("ImGui", PURPLE), ("CSV", ORANGE), ("Telemetry", RED)]:
        x = chip(c, x, PAGE_H - 248, label, col)
    text(c, 56, 115, "AUTHOR", 8, colors.HexColor("#E2E8F0"), FONT_BOLD)
    text(c, 56, 90, "TAKU OKUBO / オオクボ タク", 18, colors.white, FONT_BOLD)
    text(c, 56, 64, "ゲームプログラマー志望 / 日本工学院専門学校 ゲームクリエイター科 4年制", 9.2, colors.HexColor("#F8FAFC"))


def page_index(c: canvas.Canvas) -> None:
    y = title(c, "00", "CONTENTS", "目次", "作品概要から設計、実装、検証、今後の拡張までを順に説明。")
    items = [
        ("01", "制作情報", "期間 / 人数 / 担当範囲 / 使用ライブラリ"),
        ("02", "技術的な注力点", "DX12、scene設計、CSV、衝突、検証"),
        ("03", "作品概要", "KuboEngine / Octopus のゲーム構成"),
        ("04", "アーキテクチャ", "GameSceneFactory と PlayScene"),
        ("05", "ゲーム進行", "GameplayFlowController"),
        ("06", "プレイヤー・武器", "PlayerManager と WeaponController"),
        ("07", "敵・衝突・負荷監視", "空間分割、OBB、SoftCapTelemetry"),
        ("08", "レベルアップ", "LevelUpChoiceService と重み調整"),
        ("09", "データ駆動", "CSV採用理由、strict parse、テスト"),
        ("10", "UI / Debug / 演出", "HUD、MiniMap、DebugContext"),
        ("11", "描画・リソース", "FrameContext、SRV、cache"),
        ("12", "まとめ", "改善履歴、検証、今後の拡張"),
    ]
    y -= 20
    col_w = (CONTENT_W - 16) / 2
    for i, (no, head, body) in enumerate(items):
        x = MARGIN_X + (i % 2) * (col_w + 16)
        yy = y - (i // 2) * 59
        box(c, x, yy, col_w, 46, f"{no}  {head}", body, [BLUE, CYAN, GREEN, ORANGE][i % 4], PAPER, 7.6)
    footer(c, 2)


def page_production(c: canvas.Canvas) -> None:
    y = title(c, "01", "PRODUCTION INFO", "制作情報 / 担当範囲", "個人制作として、ゲーム部分だけでなくエンジン基盤、データ、検証ツールまで担当。")
    y -= 16
    left = (CONTENT_W - 18) * 0.45
    right = CONTENT_W - left - 18
    box(c, MARGIN_X, y, left, 78, "制作期間", "2025/07〜開発中\n延べ時間：約350〜400時間", BLUE)
    box(c, MARGIN_X, y - 94, left, 78, "制作人数", "個人制作\nプログラム全般を担当", GREEN)
    box(c, MARGIN_X, y - 188, left, 96, "担当範囲", "engine/、game/directxgame/、Resources/DirectXGame/data/、tools/、tests/ の実装・調整。", ORANGE)
    box(c, MARGIN_X + left + 18, y, right, 268, "使用ライブラリ / 技術", "C++20 / MSVC / Visual Studio\nDirectX 12 / HLSL / Windows API\nDirectXTex / Assimp\nDear ImGui / ImGuizmo / ImPlot / imgui-node-editor\nFontAwesome icon font\nCPU regression checks / scene transition stress / PIX preflight", CYAN, colors.white, 8.5)
    y -= 322
    text(c, MARGIN_X, y, "制作で意識したこと", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "ゲームを動かすだけでなく、scene遷移、resource寿命、調整データ、検証を自分で扱う。",
        "固定値をCSVへ逃がし、武器・敵・UIを短い周期で調整できるようにする。",
        "debug UI、telemetry、CPU testで、実装の壊れ方を確認しやすくする。",
    ], CONTENT_W)
    footer(c, 3)


def page_focus(c: canvas.Canvas) -> None:
    y = title(c, "02", "TECHNICAL FOCUS", "技術的な注力ポイント", "企業提出用として、実装力が伝わる3領域に紙面を寄せる。")
    y -= 12
    w = (CONTENT_W - 24) / 3
    sections = [
        ("DX12 リソース管理", ["FrameContextでcommand allocator、upload arena、deferred release、fence valueをframe単位に管理。", "SrvManagerでdescriptor heapのallocate/freeとhigh-watermarkを記録。", "GameTextureCache / GameModelCacheで重複ロードと利用側APIを整理。"], BLUE),
        ("衝突・負荷監視", ["EnemyCollisionContextでspatialMapとnearbyEnemiesを保持し、近傍候補を絞る。", "OBB narrow phaseで回転・スケールを含む当たり判定を扱う。", "SoftCapTelemetryで敵数、弾数、EXP Orb、prune数を確認する。"], GREEN),
        ("CSV データ駆動", ["武器、敵、level-up重み、UI配置、debug値をCSVへ分離。", "CSV採用理由は、表形式データをExcelとGit差分で確認しやすいため。", "CsvReaderのstrict parseとCPU testで不正値を検出する。"], ORANGE),
    ]
    for i, (head, items, col) in enumerate(sections):
        x = MARGIN_X + i * (w + 12)
        box(c, x, y, w, 260, head, "", col, colors.white)
        bullets(c, x + 14, y - 48, items, w - 28, 8.2, 11.8, col)
    footer(c, 4)


def page_feature(c: canvas.Canvas) -> None:
    y = title(c, "03", "FEATURED GAME", "KuboEngine / Octopus", "敵を倒してEXPを集め、レベルアップでビルドを強化していく3Dサバイバルシューティング。")
    y -= 12
    placeholder(c, MARGIN_X, y, 330, 172, "Gameplay Screenshot")
    x = MARGIN_X + 354
    labels = [("Genre", "3D Shooting / Survival Action"), ("Role", "1人制作 / コード全般"), ("Engine", "自作 DirectX 12 エンジン"), ("Data", "CSVによるバランス・UI調整")]
    for i, (head, body) in enumerate(labels):
        box(c, x + (i % 2) * 190, y - (i // 2) * 78, 172, 62, head, body, [BLUE, CYAN, GREEN, ORANGE][i], colors.white, 8.0)
    y -= 210
    text(c, MARGIN_X, y, "ゲームループ", 14, NAVY, FONT_BOLD)
    loop = [("Enemy", RED), ("EXP", GREEN), ("Level Up", BLUE), ("Build", PURPLE), ("Boss", ORANGE)]
    lx = MARGIN_X + 55
    ly = y - 34
    lw = 110
    for i, (label, col) in enumerate(loop):
        x = lx + i * 138
        node(c, x, ly, lw, 34, label, col)
        if i < len(loop) - 1:
            arrow(c, x + lw + 4, ly - 17, x + 134, ly - 17)
    footer(c, 5)


def page_arch(c: canvas.Canvas) -> None:
    y = title(c, "04", "ARCHITECTURE", "シーン構成と責務分離", "`main.cpp`は起動に集中し、scene生成はGameSceneFactoryへ渡す。PlaySceneはgameplayのcomposition root。")
    y -= 10
    n1 = node(c, 78, y, 120, 42, "WinMain\nApplication", NAVY)
    n2 = node(c, 252, y, 136, 42, "Engine::Scene::Game", BLUE)
    n3 = node(c, 442, y, 142, 42, "GameSceneFactory", CYAN)
    arrow(c, n1[0] + n1[2] + 5, y - 21, n2[0] - 5, y - 21)
    arrow(c, n2[0] + n2[2] + 5, y - 21, n3[0] - 5, y - 21)
    sy = y - 102
    bus_y = y - 72
    c.setStrokeColor(LINE)
    c.setLineWidth(1.35)
    c.line(513, y - 42, 513, bus_y)
    c.line(179, bus_y, 615, bus_y)
    scenes = [(114, "TitleScene", GREEN), (332, "PlayScene", ORANGE), (550, "ResultScene", PURPLE)]
    for x, label, col in scenes:
        node(c, x, sy, 130, 38, label, col)
        arrow(c, x + 65, bus_y, x + 65, sy)
    node(c, 648, y - 8, 126, 50, "GameSession\nRunResult", RED)
    y -= 180
    left = 410
    text(c, MARGIN_X, y, "コード証拠: main.cpp", 12, NAVY, FONT_BOLD)
    code_block(c, MARGIN_X, y - 20, left, [
        "auto directXGame = std::make_unique<Engine::Scene::Game>(",
        "    std::make_unique<DirectXGame::GameSceneFactory>(),",
        "    DirectXGame::SceneId::kTitle);",
        "std::unique_ptr<Engine::Base::Framework> game = std::move(directXGame);",
        "game->Run();",
    ], 7.0)
    box(c, MARGIN_X + left + 24, y, CONTENT_W - left - 24, 126, "設計意図", "起動処理、共通Framework、ゲーム固有sceneの変更理由を分ける。新しいsceneや遷移を追加する場合も、main loopへ変更が広がりにくい。", BLUE, PAPER, 8.4)
    footer(c, 6)


def page_flow(c: canvas.Canvas) -> None:
    y = title(c, "05", "GAME FLOW", "GameplayFlowController", "ゲーム中の状態を明示し、入力、world update、HUD、演出、scene遷移の切り替えを管理。")
    y -= 12
    states = [("Start", BLUE), ("Playing", GREEN), ("BossIntro", ORANGE), ("Boss", RED), ("BossDefeated", PURPLE), ("Paused", CYAN), ("LevelUp", ORANGE), ("Dead", RED)]
    w = (CONTENT_W - 36) / 4
    centers: list[tuple[float, float]] = []
    for i, (label, col) in enumerate(states):
        x = MARGIN_X + (i % 4) * (w + 12)
        yy = y - (i // 4) * 78
        node(c, x, yy, w, 42, label, col)
        centers.append((x + w / 2, yy - 21))
    for i in range(3):
        arrow(c, centers[i][0] + w / 2 - 4, centers[i][1], centers[i + 1][0] - w / 2 + 4, centers[i + 1][1])
    for i in range(4, 7):
        arrow(c, centers[i][0] + w / 2 - 4, centers[i][1], centers[i + 1][0] - w / 2 + 4, centers[i + 1][1])
    y -= 190
    text(c, MARGIN_X, y, "状態管理の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "プレイ中、ポーズ中、レベルアップ中で、world update、HUD表示、cursor表示を切り替える。",
        "ボス登場、撃破、死亡演出をPresentation系クラスへ分離し、PlaySceneの肥大化を抑える。",
        "GameSessionがrun count、scene visit count、result情報を保持し、sceneをまたぐ状態を集約する。",
    ], CONTENT_W, 8.7, 12.8)
    footer(c, 7)


def page_player_weapon(c: canvas.Canvas) -> None:
    y = title(c, "06", "PLAYER / WEAPON", "成長と武器制御", "PlayerManagerを窓口にし、進行状態と武器処理をPlayerProgression / PlayerWeaponControllerへ分割。")
    y -= 12
    node(c, 88, y, 150, 44, "PlayerManager\nFacade", NAVY)
    node(c, 322, y, 150, 44, "PlayerProgression\nHP / EXP / Lv", BLUE)
    node(c, 556, y, 154, 44, "PlayerWeaponController\n4 Weapon Types", CYAN)
    arrow(c, 242, y - 22, 318, y - 22)
    arrow(c, 474, y - 22, 552, y - 22)
    y -= 88
    w = (CONTENT_W - 24) / 4
    for i, (head, body, col) in enumerate([("Normal", "前方連射 / active cap 96", BLUE), ("Orbit", "円軌道 / 弾数・半径・回転速度", GREEN), ("Drone", "自動射撃 / ショット数・間隔", ORANGE), ("Lightning", "範囲攻撃 / 対象数・演出", PURPLE)]):
        box(c, MARGIN_X + i * (w + 8), y, w, 58, head, body, col, PAPER, 7.5)
    y -= 98
    text(c, MARGIN_X, y, "実装上の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "`WeaponType` と `WeaponUpgradeData` により、武器種別と成長値を同じ流れで扱う。",
        "`weaponUpgradeSettings.csv` から弾数、間隔、半径、貫通、ダメージ補正を読み込む。",
        "武器追加や成長値変更がPlayer本体へ直接広がらないよう、武器制御を専用クラスへ寄せた。",
    ], CONTENT_W, 8.6, 12.6)
    footer(c, 8)


def page_collision(c: canvas.Canvas) -> None:
    y = title(c, "07", "ENEMY / COLLISION", "敵管理・OBB衝突・負荷監視", "敵や弾が増える場面で、候補数、当たり判定、オブジェクト上限をまとめて扱う。")
    y -= 10
    placeholder(c, MARGIN_X, y, 252, 142, "OBB Debug Screenshot")
    x = MARGIN_X + 274
    bullets(c, x, y - 4, [
        "`EnemyCollisionContext` が `spatialMap`, `activeEnemies`, `nearbyEnemies` を保持。",
        "`EnemyCollisionSystem` がRebuild、Check、AreaDamage、Separationを担当。",
        "`SoftCapTelemetry` でenemy数、kill数、EXP Orb、弾数、prune数を記録。",
    ], CONTENT_W - 274, 8.2, 11.8, GREEN)
    y -= 166
    text(c, MARGIN_X, y, "コード証拠: MakeCellKey", 12, NAVY, FONT_BOLD)
    code_block(c, MARGIN_X, y - 20, 370, [
        "inline uint64_t MakeCellKey(int32_t cellX, int32_t cellZ)",
        "{",
        "    return (static_cast<uint64_t>(",
        "        static_cast<uint32_t>(cellX)) << 32) |",
        "        static_cast<uint32_t>(cellZ);",
        "}",
    ], 7.0)
    box(c, MARGIN_X + 392, y - 20, CONTENT_W - 392, 108, "設計意図", "負座標のcellも扱うため、`uint32_t` 経由で `uint64_t` にpackする。これにより、負値を左shiftする未定義動作を避ける。", ORANGE, PAPER, 8.2)
    footer(c, 9)


def page_levelup(c: canvas.Canvas) -> None:
    y = title(c, "08", "LEVEL-UP SYSTEM", "LevelUpChoiceService", "候補生成と適用処理を分離し、武器所持・レベル・HP状態に応じて候補を切り替える。")
    y -= 12
    left_w = 350
    text(c, MARGIN_X, y, "候補データ", 13, NAVY, FONT_BOLD)
    code_block(c, MARGIN_X, y - 22, left_w, [
        "enum class LevelUpUpgrade {",
        "  Normal, Orbit, Drone, Lightning,",
        "  Attack, MaxHp, MoveSpeed, Heal",
        "};",
        "",
        "struct LevelUpChoice {",
        "  LevelUpUpgrade upgrade;",
        "  std::string texturePath;",
        "  std::string iconPath;",
        "};",
    ], 7.1)
    rx = MARGIN_X + left_w + 34
    text(c, rx, y, "候補決定の流れ", 13, NAVY, FONT_BOLD)
    yy = y - 22
    steps = [("Player状態", "HP、所持武器、各武器Lv、上限到達を参照"), ("Pool生成", "未所持武器は解禁、所持済みは強化候補へ切替"), ("Weight", "`levelupWeights.csv` で出現比率を調整"), ("Apply", "`PlayerManager` のUpgrade/Add/Heal APIを呼ぶ")]
    for head, body in steps:
        box(c, rx, yy, CONTENT_W - left_w - 34, 46, head, body, BLUE, PAPER, 7.8)
        yy -= 56
    footer(c, 10)


def page_data(c: canvas.Canvas) -> None:
    y = title(c, "09", "CSV DRIVEN", "データ駆動型の調整基盤", "CSVを採用した理由、弱点、対策を明記し、実装判断が伝わるページにする。")
    y -= 10
    w = (CONTENT_W - 24) / 4
    csvs = [("CSV", "Excel編集、Git差分、表形式データと相性が良い", BLUE), ("JSON", "階層構造には強いが、武器Lv表では冗長", GREEN), ("Binary", "読み込み効率は良いが、手編集とdebugが難しい", ORANGE), ("C++直書き", "型は強いが、調整ごとにbuildが必要", PURPLE)]
    for i, (head, body, col) in enumerate(csvs):
        box(c, MARGIN_X + i * (w + 8), y, w, 74, head, body, col, colors.white, 7.3)
    y -= 105
    text(c, MARGIN_X, y, "strict parse", 13, NAVY, FONT_BOLD)
    code_block(c, MARGIN_X, y - 20, 390, [
        "const float result = std::stof(text, &parsedLength);",
        "if (parsedLength != text.size() || !std::isfinite(result)) {",
        "    throw std::invalid_argument(\"not a finite float\");",
        "}",
    ], 7.0)
    box(c, MARGIN_X + 414, y - 20, CONTENT_W - 414, 86, "CPU test", "`1.0x`, `nan`, `12.0`, overflowなどを不正値として検出。CSVの編集しやすさと型の弱さを、parseとtestで補う。", ORANGE, PAPER, 8.0)
    y -= 132
    text(c, MARGIN_X, y, "主なCSV", 13, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 24, [
        "`playerStatus.csv`, `weaponUpgradeSettings.csv`, `enemyTypes.csv`, `enemySpawnSettings.csv`",
        "`levelupWeights.csv`, `ui_layout_title/hud/levelup/pause/result.csv`",
        "`resource_manifest.csv`, `debug_tuning.csv`, `soft_cap_telemetry.csv`",
    ], CONTENT_W, 8.0, 11.2, BLUE)
    footer(c, 11)


def page_ui_debug(c: canvas.Canvas) -> None:
    y = title(c, "10", "UI / DEBUG", "HUD・ミニマップ・調整環境", "実行中の状態確認、当たり判定表示、telemetry確認を行うためのdebug環境。")
    y -= 10
    placeholder(c, MARGIN_X, y, 292, 140, "Debug UI Screenshot")
    x = MARGIN_X + 314
    col_w = (CONTENT_W - 314 - 12) / 2
    cards = [("DebugContext", "freeze、debug state、editor連携を保持。", BLUE), ("DebugUI::*", "Input、Scene、Runtime、Rendering、Visualをnamespaceで整理。", GREEN), ("MiniMap", "敵・EXP Orb・playerを円形範囲にclampして表示。", PURPLE), ("Telemetry", "敵数、弾数、EXP Orb、particle数を確認。", ORANGE)]
    for i, (head, body, col) in enumerate(cards):
        box(c, x + (i % 2) * (col_w + 12), y - (i // 2) * 72, col_w, 56, head, body, col, PAPER, 7.4)
    y -= 176
    text(c, MARGIN_X, y, "確認サイクル", 14, NAVY, FONT_BOLD)
    cycle = [("Play", BLUE), ("Inspect", CYAN), ("Tune", GREEN), ("Save CSV", ORANGE)]
    for i, (label, col) in enumerate(cycle):
        x = MARGIN_X + 92 + i * 152
        node(c, x, y - 30, 104, 34, label, col)
        if i < len(cycle) - 1:
            arrow(c, x + 108, y - 47, x + 148, y - 47)
    footer(c, 12)


def page_rendering_resource(c: canvas.Canvas) -> None:
    y = title(c, "11", "RENDERING / RESOURCE", "描画・リソース寿命管理", "DirectX 12ではGPU参照中のresource寿命を明示する必要があるため、FrameContextとSrvManagerを中心に管理。")
    y -= 10
    left = 402
    text(c, MARGIN_X, y, "FrameContext", 13, NAVY, FONT_BOLD)
    code_block(c, MARGIN_X, y - 20, left, [
        "struct FrameContext",
        "{",
        "    ComPtr<ID3D12CommandAllocator> commandAllocator;",
        "    ComPtr<ID3D12Resource> uploadArena;",
        "    std::vector<ComPtr<ID3D12Resource>> deferredReleaseResources;",
        "    uint64_t fenceValue = 0;",
        "};",
    ], 6.8)
    x = MARGIN_X + left + 24
    box(c, x, y, CONTENT_W - left - 24, 72, "SrvManager", "SRV heapを単独所有し、allocate/free、used count、high-watermark、usage recordを記録。", BLUE, PAPER, 8.0)
    box(c, x, y - 90, CONTENT_W - left - 24, 72, "Cache", "GameTextureCache、GameModelCacheで重複ロードと利用側APIを整理。", GREEN, PAPER, 8.0)
    y -= 210
    text(c, MARGIN_X, y, "frame reuse", 13, NAVY, FONT_BOLD)
    node(c, MARGIN_X + 80, y - 30, 124, 36, "Frame 0\nupload / release", BLUE)
    node(c, MARGIN_X + 300, y - 30, 124, 36, "GPU fence\ncomplete", TEAL)
    node(c, MARGIN_X + 520, y - 30, 124, 36, "Reuse frame\nclear release", ORANGE)
    arrow(c, MARGIN_X + 208, y - 48, MARGIN_X + 296, y - 48)
    arrow(c, MARGIN_X + 428, y - 48, MARGIN_X + 516, y - 48)
    footer(c, 13)


def page_summary(c: canvas.Canvas) -> None:
    y = title(c, "12", "SUMMARY", "まとめ / 改善履歴 / 今後の拡張", "実装した機能を並べるだけでなく、設計上どこを直し、何を検証したかを示す。")
    y -= 10
    col_w = (CONTENT_W - 18) / 2
    items = [
        ("Scene安定性", "GameSceneFactory、GameSession、RunResultへ整理し、scene遷移と結果共有の責務を明確化。", BLUE),
        ("Resource寿命", "FrameContext、deferred release、SrvManager high-watermarkでDX12 resourceの寿命を追える形にした。", GREEN),
        ("CSV安全性", "strict parseとCPU regression checksで、編集しやすさと壊れ方の検出を両立。", ORANGE),
        ("負荷監視", "SoftCapTelemetryとscene transition stressで、弾・EXP Orb・descriptor使用量を数値で確認。", PURPLE),
    ]
    for i, (head, body, col) in enumerate(items):
        x = MARGIN_X + (i % 2) * (col_w + 18)
        yy = y - (i // 2) * 78
        box(c, x, yy, col_w, 60, head, body, col, colors.white, 7.6)
    y -= 176
    text(c, MARGIN_X, y, "検証値", 13, NAVY, FONT_BOLD)
    code_block(c, MARGIN_X, y - 20, 340, [
        "scene transition stress: PASS",
        "title/game/result visits: 3 / 3 / 3",
        "maxSrvUsed: 105",
        "maxSrvHighWatermark: 105",
    ], 7.0)
    box(c, MARGIN_X + 364, y - 20, CONTENT_W - 364, 92, "今後の拡張", "collision benchmark、PIX capture、gameplay/OBB/debug screenshotsを追加し、実装内容を画面と計測値でさらに説明できる状態にする。", RED, PAPER, 8.0)
    footer(c, 14)


PAGES = [
    page_cover,
    page_index,
    page_production,
    page_focus,
    page_feature,
    page_arch,
    page_flow,
    page_player_weapon,
    page_collision,
    page_levelup,
    page_data,
    page_ui_debug,
    page_rendering_resource,
    page_summary,
]


def build() -> None:
    register_fonts()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(OUT_PDF), pagesize=landscape(A4))
    c.setTitle("プログラム説明資料 - KuboEngine 改訂版")
    c.setAuthor("TAKU OKUBO")
    for i, page in enumerate(PAGES):
        page(c)
        if i != len(PAGES) - 1:
            c.showPage()
    c.save()
    print(OUT_PDF)


if __name__ == "__main__":
    build()
