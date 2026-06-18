from __future__ import annotations

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "output" / "pdf"
OUT_PDF = OUT_DIR / "program_explanation_A4_2026.pdf"

PAGE_W, PAGE_H = landscape(A4)
MARGIN_X = 42
TOP_Y = PAGE_H - 34
BOTTOM_Y = 36
CONTENT_W = PAGE_W - MARGIN_X * 2

FONT_REG = "BizUDGothicRegular"
FONT_BOLD = "BizUDGothicBold"
FONT_MONO = "Courier"

NAVY = colors.HexColor("#0B1220")
INK = colors.HexColor("#111827")
LINE = colors.HexColor("#CBD5E1")
BLUE = colors.HexColor("#2563EB")
CYAN = colors.HexColor("#06B6D4")
GREEN = colors.HexColor("#10B981")
ORANGE = colors.HexColor("#F59E0B")
RED = colors.HexColor("#EF4444")
PURPLE = colors.HexColor("#7C3AED")
PAPER = colors.HexColor("#F8FAFC")


def register_fonts() -> None:
    pdfmetrics.registerFont(TTFont(FONT_REG, r"C:\Windows\Fonts\BIZ-UDGothicR.ttc"))
    pdfmetrics.registerFont(TTFont(FONT_BOLD, r"C:\Windows\Fonts\BIZ-UDGothicB.ttc"))


def sw(value: str, size: float, font: str = FONT_REG) -> float:
    return pdfmetrics.stringWidth(value, font, size)


def fit_lines(value: str, max_w: float, size: float, font: str = FONT_REG) -> list[str]:
    lines: list[str] = []
    for raw in str(value).splitlines():
        raw = raw.strip()
        if not raw:
            lines.append("")
            continue
        current = ""
        for ch in raw:
            test = current + ch
            if not current or sw(test, size, font) <= max_w:
                current = test
            else:
                lines.append(current)
                current = ch
        if current:
            lines.append(current)
    return lines


def text(c: canvas.Canvas, x: float, y: float, value: str, size: float = 10, color=INK, font: str = FONT_REG) -> None:
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawString(x, y, value)


def right_text(c: canvas.Canvas, x: float, y: float, value: str, size: float = 10, color=INK, font: str = FONT_REG) -> None:
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawRightString(x, y, value)


def paragraph(c: canvas.Canvas, x: float, y: float, value: str, max_w: float, size: float = 9.0, leading: float = 12.5, color=INK, font: str = FONT_REG) -> float:
    for line in fit_lines(value, max_w, size, font):
        text(c, x, y, line, size, color, font)
        y -= leading
    return y


def title(c: canvas.Canvas, no: str, label: str, main: str, sub: str) -> float:
    text(c, MARGIN_X, TOP_Y, f"{no} / {label}", 8.2, BLUE, FONT_BOLD)
    text(c, MARGIN_X, TOP_Y - 28, main, 24, NAVY, FONT_BOLD)
    return paragraph(c, MARGIN_X, TOP_Y - 50, sub, CONTENT_W, 9.2, 12.5)


def footer(c: canvas.Canvas, page: int) -> None:
    c.setStrokeColor(LINE)
    c.setLineWidth(1)
    c.line(MARGIN_X, 26, PAGE_W - MARGIN_X, 26)
    text(c, MARGIN_X, 13, "KuboEngine / Program Explanation", 7, INK)
    right_text(c, PAGE_W - MARGIN_X, 13, f"{page:02d}", 7, INK)


def chip(c: canvas.Canvas, x: float, y: float, label: str, fill) -> float:
    size = 8
    w = sw(label, size, FONT_BOLD) + 16
    c.setFillColor(fill)
    c.roundRect(x, y - 12, w, 18, 5, stroke=0, fill=1)
    text(c, x + 8, y - 7, label, size, colors.white, FONT_BOLD)
    return x + w + 6


def box(c: canvas.Canvas, x: float, y: float, w: float, h: float, head: str, body: str = "", accent=BLUE, fill=colors.white, body_size: float = 8.2) -> None:
    c.setFillColor(fill)
    c.setStrokeColor(LINE)
    c.roundRect(x, y - h, w, h, 7, stroke=1, fill=1)
    c.setFillColor(accent)
    c.roundRect(x, y - 5, w, 5, 3, stroke=0, fill=1)
    text(c, x + 12, y - 23, head, 11.5, INK, FONT_BOLD)
    if body:
        paragraph(c, x + 12, y - 42, body, w - 24, body_size, body_size + 4, INK)


def bullets(c: canvas.Canvas, x: float, y: float, items: list[str], max_w: float, size: float = 8.7, leading: float = 12.2, color=BLUE) -> float:
    for item in items:
        c.setFillColor(color)
        c.circle(x + 3, y + 3, 2, stroke=0, fill=1)
        for line in fit_lines(item, max_w - 17, size):
            text(c, x + 15, y, line, size, INK)
            y -= leading
        y -= 2
    return y


def node(c: canvas.Canvas, x: float, y: float, w: float, h: float, label: str, fill) -> None:
    c.setFillColor(fill)
    c.roundRect(x, y - h, w, h, 6, stroke=0, fill=1)
    lines = fit_lines(label, w - 12, 8.2, FONT_BOLD)
    yy = y - h / 2 + (len(lines) - 1) * 5
    for line in lines:
        text(c, x + w / 2 - sw(line, 8.2, FONT_BOLD) / 2, yy, line, 8.2, colors.white, FONT_BOLD)
        yy -= 10


def arrow(c: canvas.Canvas, x1: float, y1: float, x2: float, y2: float) -> None:
    c.setStrokeColor(LINE)
    c.setLineWidth(1.3)
    c.line(x1, y1, x2, y2)
    dx = 1 if x2 >= x1 else -1
    c.line(x2, y2, x2 - 5 * dx, y2 + 3)
    c.line(x2, y2, x2 - 5 * dx, y2 - 3)


def code_block(c: canvas.Canvas, x: float, y: float, w: float, lines: list[str]) -> float:
    size = 8.0
    leading = 11.7
    pad_x = 14
    pad_top = 15
    pad_bottom = 13
    wrapped: list[str] = []
    max_w = w - pad_x * 2
    for line in lines:
        if not line:
            wrapped.append("")
            continue
        while sw(line, size, FONT_MONO) > max_w:
            cut = max(12, int(max_w / sw("M", size, FONT_MONO)))
            wrapped.append(line[:cut])
            line = "  " + line[cut:]
        wrapped.append(line)
    h = pad_top + pad_bottom + len(wrapped) * leading
    c.setFillColor(colors.HexColor("#0F172A"))
    c.roundRect(x, y - h, w, h, 7, stroke=0, fill=1)
    yy = y - pad_top
    for line in wrapped:
        text(c, x + pad_x, yy, line, size, colors.HexColor("#F8FAFC"), FONT_MONO)
        yy -= leading
    return y - h


def page_cover(c: canvas.Canvas) -> None:
    c.setFillColor(NAVY)
    c.rect(0, 0, PAGE_W, PAGE_H, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#101A2D"))
    c.circle(PAGE_W - 72, PAGE_H - 42, 130, stroke=0, fill=1)
    c.setFillColor(colors.HexColor("#0F766E"))
    c.circle(PAGE_W - 28, 66, 88, stroke=0, fill=1)
    text(c, 54, PAGE_H - 75, "PROGRAMMER // 2026", 10, CYAN, FONT_BOLD)
    text(c, 54, PAGE_H - 143, "プログラム説明資料", 36, colors.white, FONT_BOLD)
    text(c, 56, PAGE_H - 180, "KuboEngine / Octopus", 18, colors.white, FONT_BOLD)
    paragraph(c, 56, PAGE_H - 215, "DirectX 12 × C++ × HLSL。設計、データ駆動、負荷対策、開発効率化を中心に構成。", 560, 10.6, 16, colors.HexColor("#F8FAFC"))
    x = 56
    for label, col in [("C++", BLUE), ("DirectX 12", CYAN), ("HLSL", GREEN), ("ImGui", PURPLE), ("CSV Driven", ORANGE), ("Performance", RED)]:
        x = chip(c, x, PAGE_H - 248, label, col)
    text(c, 56, 115, "AUTHOR", 8, colors.HexColor("#E2E8F0"), FONT_BOLD)
    text(c, 56, 90, "TAKU OKUBO / オオクボ タク", 18, colors.white, FONT_BOLD)
    text(c, 56, 64, "ゲームプログラマー志望 / 日本工学院専門学校 ゲームクリエイター科 4年制", 9.2, colors.HexColor("#F8FAFC"))


def page_index(c: canvas.Canvas) -> None:
    y = title(c, "00", "CONTENTS", "目次", "技術的に工夫した点、制作情報、担当範囲、使用ライブラリを整理。")
    items = [
        ("01", "制作情報", "期間 / 人数 / 担当箇所 / 使用ライブラリ"),
        ("02", "技術的な注力点", "設計、データ駆動、負荷対策"),
        ("03", "作品概要", "KuboEngine / Octopus のゲーム構成"),
        ("04", "アーキテクチャ", "SceneFactory と SessionContext"),
        ("05", "ゲーム進行", "GameplayFlowController による状態管理"),
        ("06", "プレイヤー・武器", "成長と 4 種類の武器制御"),
        ("07", "敵・衝突・性能", "空間分割と Telemetry"),
        ("08", "レベルアップ", "候補生成と CSV 重み調整"),
        ("09", "データ駆動", "CSV による調整基盤"),
        ("10", "UI / Debug / 演出", "HUD、MiniMap、ImGui、Presentation"),
        ("11", "まとめ", "学びと今後の改善"),
    ]
    y -= 20
    col_w = (CONTENT_W - 16) / 2
    for i, (no, head, body) in enumerate(items):
        x = MARGIN_X + (i % 2) * (col_w + 16)
        yy = y - (i // 2) * 59
        box(c, x, yy, col_w, 46, f"{no}  {head}", body, [BLUE, CYAN, GREEN, ORANGE][i % 4], PAPER, 7.7)
    footer(c, 2)


def page_production(c: canvas.Canvas) -> None:
    y = title(c, "01", "PRODUCTION INFO", "制作情報 / 担当範囲", "制作形態、担当箇所、使用した外部ライブラリを明記。")
    y -= 22
    left = (CONTENT_W - 18) * 0.45
    right = CONTENT_W - left - 18
    box(c, MARGIN_X, y, left, 82, "制作期間", "2025/07〜開発中（継続制作）\n週7〜8時間程度で制作\n延べ時間：約350〜400時間", BLUE)
    box(c, MARGIN_X, y - 100, left, 82, "制作人数", "個人制作\nプログラマ 1 名のみ", GREEN)
    box(c, MARGIN_X, y - 200, left, 94, "担当箇所", "コード全般を担当。\nengine/、game/directxgame/、Resources/DirectXGame/data/ などの実装・調整を担当。", ORANGE)
    box(
        c,
        MARGIN_X + left + 18,
        y,
        right,
        276,
        "使用ライブラリ / 外部コード",
        "DirectX 12 / HLSL\nDear ImGui / ImGuizmo / imgui-node-editor / ImPlot\nDirectXTex / Assimp / FontAwesome IconFont\nWindows API / XAudio 系の音再生基盤\n学内配布・外部導入コードを含む部分は externals/ 配下に分離",
        CYAN,
        colors.white,
        8.6,
    )
    y -= 330
    text(c, MARGIN_X, y, "制作で意識したこと", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "ゲーム性だけでなく、仕様変更に耐えるクラス分割を意識して実装。",
        "数値調整を CSV に分離し、コード変更なしでバランス調整できる状態を構築。",
        "デバッグ UI とテレメトリで、感覚だけではなく数値を見て改善。",
    ], CONTENT_W)
    footer(c, 3)


def page_focus(c: canvas.Canvas) -> None:
    y = title(c, "02", "TECHNICAL FOCUS", "技術的な注力ポイント", "現行プログラムで特に工夫した設計、調整しやすさ、負荷対策を中心に整理。")
    y -= 18
    w = (CONTENT_W - 24) / 3
    sections = [
        ("設計・責務分離", ["Scene、Player、Enemy、UI、演出の責務を分け、変更時の影響範囲を小さくした。", "進行管理、武器制御、敵生成、衝突、演出を専用クラス化。", "Presentation 層を分け、ゲームロジックの見通しを維持。"], BLUE),
        ("データ駆動・調整", ["CSV で武器強化、敵出現、レベルアップ重み、UI 配置を外部化。", "ImGui と連携し、実行中に状態確認と調整を行える開発環境を構築。", "manifest でロード対象を一覧化し、抜け漏れを削減。"], GREEN),
        ("負荷対策・見える化", ["敵最大 84 体と複数武器の衝突を空間分割で近傍判定に限定。", "通常弾、EXP Orb などの peak / prune を監視。", "SoftCapTelemetry で負荷状況を CSV 出力。"], ORANGE),
    ]
    for i, (head, items, col) in enumerate(sections):
        x = MARGIN_X + i * (w + 12)
        box(c, x, y, w, 250, head, "", col, colors.white)
        bullets(c, x + 14, y - 48, items, w - 28, 8.3, 12, col)
    footer(c, 4)


def page_feature(c: canvas.Canvas) -> None:
    y = title(c, "03", "FEATURED GAME", "KuboEngine / Octopus", "敵を倒して EXP を集め、レベルアップでビルドを強化していく 3D サバイバルシューティング。")
    y -= 18
    labels = [("Genre", "3D Shooting / Survival Action"), ("Role", "1人制作 / コード全般"), ("Engine", "自作 DirectX 12 エンジン"), ("Data", "CSV によるバランス・UI 調整")]
    for i, (head, body) in enumerate(labels):
        box(c, MARGIN_X + i * 190, y, 172, 64, head, body, [BLUE, CYAN, GREEN, ORANGE][i], colors.white, 8.2)
    y -= 106
    text(c, MARGIN_X, y, "現在の主なゲーム要素", 14, NAVY, FONT_BOLD)
    cards = [
        ("Scene / Flow", "Title / Game / Result とゲーム中状態を分けて管理。", BLUE),
        ("Combat", "通常弾、衛星弾、ドローン、雷撃を統合。", CYAN),
        ("Growth", "状態に応じてレベルアップ候補を生成し、PlayerManager へ適用。", GREEN),
        ("Enemy", "敵生成、衝突、EXP Orb、ボス状態を管理。", ORANGE),
        ("Presentation", "Boss、戦闘、死亡、遷移演出を専用クラスへ分離。", PURPLE),
    ]
    card_w = (CONTENT_W - 24) / 3
    for i, (head, body, col) in enumerate(cards):
        x = MARGIN_X + (i % 3) * (card_w + 12)
        yy = y - 30 - (i // 3) * 82
        box(c, x, yy, card_w, 64, head, body, col, PAPER, 8.0)
    y -= 206
    text(c, MARGIN_X, y, "ゲームループの概要", 14, NAVY, FONT_BOLD)
    loop = [("Enemy", RED), ("EXP", GREEN), ("Level Up", BLUE), ("Build", PURPLE), ("Boss", ORANGE)]
    lx = MARGIN_X + 54
    ly = y - 34
    lw = 110
    for i, (label, col) in enumerate(loop):
        x = lx + i * 138
        node(c, x, ly, lw, 34, label, col)
        if i < len(loop) - 1:
            arrow(c, x + lw, ly - 17, x + 138, ly - 17)
    footer(c, 5)


def page_arch(c: canvas.Canvas) -> None:
    y = title(c, "04", "ARCHITECTURE", "シーン構成と責務分離", "OS エントリーポイントから Game、SceneFactory、各 Scene へ責務を渡す構成。")
    y -= 18
    node(c, 80, y, 120, 42, "WinMain\nApplication", NAVY)
    node(c, 250, y, 130, 42, "Engine::Scene::Game", BLUE)
    node(c, 430, y, 150, 42, "DirectXGameSceneFactory", CYAN)
    arrow(c, 200, y - 21, 250, y - 21)
    arrow(c, 380, y - 21, 430, y - 21)
    sy = y - 110
    for x, label, col in [(120, "TitleScene", GREEN), (330, "GameScene", ORANGE), (540, "ResultScene", PURPLE)]:
        node(c, x, sy, 130, 38, label, col)
        arrow(c, 505, y - 42, x + 65, sy)
    node(c, 640, y - 42, 132, 50, "SessionContext\nResultData", RED)
    y -= 210
    text(c, MARGIN_X, y, "設計上の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "main.cpp は Framework 実行に集中し、具体的なシーン生成は Factory へ委譲。",
        "SessionContext が run count、scene visit count、result summary を保持。",
        "GameScene は集約 root とし、進行・UI・演出・敵・武器の責務は専用クラスへ分離。",
    ], CONTENT_W, 8.9, 13)
    footer(c, 6)


def page_flow(c: canvas.Canvas) -> None:
    y = title(c, "05", "GAME FLOW", "GameplayFlowController", "ゲーム中の状態を enum class GameplayState で管理し、入力・UI・世界更新の可否を状態から判断。")
    y -= 18
    states = [("Start", BLUE), ("Playing", GREEN), ("BossIntro", ORANGE), ("Boss", RED), ("BossDefeated", PURPLE), ("Paused", CYAN), ("LevelUp", ORANGE), ("Dead", RED)]
    w = (CONTENT_W - 36) / 4
    for i, (label, col) in enumerate(states):
        x = MARGIN_X + (i % 4) * (w + 12)
        yy = y - (i // 4) * 78
        node(c, x, yy, w, 42, label, col)
    y -= 198
    text(c, MARGIN_X, y, "状態管理の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "プレイ中・ポーズ中・レベルアップ中で、ワールド更新、HUD 表示、カーソル表示を切替。",
        "ボス登場と撃破演出をゲーム本体から分離し、カメラやエフェクト制御を専用化。",
        "死亡時は PlayerDeathPresentation と ResultScene への遷移を連携。",
    ], CONTENT_W, 8.9, 13)
    footer(c, 7)


def page_player_weapon(c: canvas.Canvas) -> None:
    y = title(c, "06", "PLAYER / WEAPON", "成長と武器制御", "PlayerManager は HP / EXP / Lv / 武器状態の窓口。内部で Progression と WeaponController に分割。")
    y -= 18
    node(c, 86, y, 150, 44, "PlayerManager\nFacade", NAVY)
    node(c, 320, y, 150, 44, "PlayerProgression\nHP / EXP / Lv", BLUE)
    node(c, 554, y, 154, 44, "WeaponController\n4 Weapon Types", CYAN)
    arrow(c, 236, y - 22, 320, y - 22)
    arrow(c, 470, y - 22, 554, y - 22)
    y -= 94
    w = (CONTENT_W - 24) / 4
    for i, (head, body, col) in enumerate([("Normal", "前方連射 / Lv8 / 最大 96 発を監視", BLUE), ("Orbit", "円軌道 / 弾数・半径・回転速度", GREEN), ("Drone", "自動射撃 / ショット数・間隔・貫通", ORANGE), ("Lightning", "範囲攻撃 / 対象数・半径・演出", PURPLE)]):
        box(c, MARGIN_X + i * (w + 8), y, w, 58, head, body, col, PAPER, 7.6)
    y -= 102
    text(c, MARGIN_X, y, "武器実装の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, [
        "武器ごとの処理は PlayerWeaponController に集約し、PlayerManager は外部公開 API と成長処理の窓口にした。",
        "通常弾・衛星弾・ドローン・雷撃を同じ成長ルールで扱い、拡張しやすくした。",
        "weaponUpgradeSettings.csv により Lv ごとの弾数、半径、間隔、貫通、ダメージ補正を調整可能にした。",
    ], CONTENT_W, 8.7, 12.6)
    footer(c, 8)


def page_collision(c: canvas.Canvas) -> None:
    y = title(c, "07", "ENEMY / COLLISION", "敵管理と空間分割", "EnemyManager は敵リストと EXP Orb を持ち、生成・衝突・撃破ドロップ・ボス状態を統合する。")
    y -= 18
    col_w = (CONTENT_W - 24) / 3
    for i, (head, body, col) in enumerate([("EnemySpawnController", "CSV から敵タイプ・スポーン設定を読み込み、経過時間に応じて出現数や間隔を制御。", BLUE), ("EnemyCollisionSystem", "spatialMap と nearbyEnemies を使い、接触し得る敵だけを検査。範囲ダメージと重なり解消も担当。", CYAN), ("Telemetry", "敵数、EXP Orb 数、弾数 peak / prune を取得し、デバッグ UI と CSV 出力で負荷を可視化。", GREEN)]):
        box(c, MARGIN_X + i * (col_w + 12), y, col_w, 118, head, body, col, colors.white, 7.9)
    y -= 170
    text(c, MARGIN_X, y, "処理フロー", 14, NAVY, FONT_BOLD)
    w = (CONTENT_W - 48) / 5
    for i, label in enumerate(["Spawn", "SpatialMap", "Collision", "Damage", "Drop / Prune"]):
        node(c, MARGIN_X + i * (w + 12), y - 36, w, 36, label, [BLUE, CYAN, GREEN, ORANGE, RED][i])
    footer(c, 9)


def page_levelup(c: canvas.Canvas) -> None:
    y = title(c, "08", "LEVEL-UP SYSTEM", "LevelUpChoiceService", "候補生成と適用処理をサービス化。武器所持・レベル・HP 状態に応じて候補を切り替える。")
    y -= 18
    left_w = 350
    text(c, MARGIN_X, y, "現行構造", 14, NAVY, FONT_BOLD)
    bottom = code_block(c, MARGIN_X, y - 22, left_w, [
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
    ])
    rx = MARGIN_X + left_w + 34
    text(c, rx, y, "候補決定の流れ", 14, NAVY, FONT_BOLD)
    yy = y - 22
    for head, body in [("Player 状態", "HP、所持武器、各武器 Lv、上限到達を参照"), ("Pool 生成", "未所持武器は解禁、所持済みは強化候補へ切替"), ("Weight", "levelupWeights.csv で出現比率を調整"), ("Apply", "PlayerManager の Upgrade / Add / Heal API を呼び出し")]:
        box(c, rx, yy, CONTENT_W - left_w - 34, 46, head, body, BLUE, PAPER, 7.8)
        yy -= 57
    paragraph(c, MARGIN_X, min(bottom, yy) - 22, "候補生成と適用処理を分け、武器追加や回復候補の調整をコード全体へ波及させにくくした。", CONTENT_W, 8.7, 12)
    footer(c, 10)


def page_data(c: canvas.Canvas) -> None:
    y = title(c, "09", "CSV DRIVEN", "データ駆動型の調整基盤", "調整値を CSV に分離し、コンパイルなしでバランス・UI・リソースを変更できるようにしている。")
    y -= 18
    w = (CONTENT_W - 24) / 4
    csvs = [("playerStatus.csv", "初期 HP、攻撃力、移動速度、上限値"), ("weaponUpgradeSettings.csv", "武器 Lv ごとの弾数、間隔、半径、貫通"), ("levelupWeights.csv", "選択肢の出現重み、HP 状況による回復重み"), ("enemyTypes.csv", "敵タイプごとの HP、速度、EXP、出現数"), ("enemySpawnSettings.csv", "最大敵数 84、スポーン距離、加速、間隔"), ("ui_layout_*.csv", "タイトル、HUD、ポーズ、結果画面の配置"), ("resource_manifest.csv", "テクスチャ、音、データファイルのロード対象"), ("debug_tuning.csv", "デバッグ調整値と検証用パラメータ")]
    for i, (head, body) in enumerate(csvs):
        x = MARGIN_X + (i % 4) * (w + 8)
        yy = y - (i // 4) * 74
        box(c, x, yy, w, 56, head, body, [BLUE, CYAN, GREEN, ORANGE][i % 4], colors.white, 7.1)
    y -= 184
    text(c, MARGIN_X, y, "調整しやすくする工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, ["マジックナンバーを減らし、数値の意味をファイル単位で追える。", "調整担当がコードを触らずにバランスや UI 配置を変更できる。", "resource_manifest.csv により、ロード対象を一覧化し抜け漏れを検出しやすい。"], CONTENT_W, 8.8, 13)
    y -= 92
    text(c, MARGIN_X, y, "調整フロー", 14, NAVY, FONT_BOLD)
    flow = [("CSV編集", BLUE), ("Reload", CYAN), ("Game反映", GREEN), ("ImGui確認", ORANGE)]
    for i, (label, col) in enumerate(flow):
        x = MARGIN_X + 72 + i * 160
        node(c, x, y - 32, 112, 34, label, col)
        if i < len(flow) - 1:
            arrow(c, x + 112, y - 49, x + 160, y - 49)
    footer(c, 11)


def page_ui_debug(c: canvas.Canvas) -> None:
    y = title(c, "10", "UI / DEBUG", "HUD・ミニマップ・調整環境", "スプライトベースの UI 基盤と ImGui デバッグ UI を使い、実行中の状態確認と調整をしやすくした。")
    y -= 18
    col_w = (CONTENT_W - 24) / 3
    cards = [("UIElement", "位置、サイズ、表示、親子関係、縦横レイアウトを共通化。", BLUE), ("GameplayHudPresentation", "Timer、HP、EXP、KeyUI、MiniMap、開始/死亡/被弾オーバーレイを管理。", GREEN), ("MiniMap", "敵・EXP Orb・プレイヤーを円形範囲にクランプして表示。", PURPLE), ("GameplayDebugUiController", "各 DebugPanel の統合とデバッグ操作の窓口。", ORANGE), ("Runtime / Statistics", "オブジェクト数、敵数、弾数、EXP Orb 数を監視。", CYAN)]
    for i, (head, body, col) in enumerate(cards):
        x = MARGIN_X + (i % 3) * (col_w + 12)
        yy = y - (i // 3) * 80
        box(c, x, yy, col_w, 62, head, body, col, PAPER, 7.6)
    y -= 192
    text(c, MARGIN_X, y, "開発効率化の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, ["DockSpace と Scene Window により、実行画面を見ながら数値・描画・音を同時に調整できる。", "DebugPanel を機能ごとに分け、検証したい対象へ素早くアクセスできるようにした。"], CONTENT_W, 8.8, 13)
    y -= 76
    text(c, MARGIN_X, y, "確認サイクル", 14, NAVY, FONT_BOLD)
    cycle = [("Play", BLUE), ("Inspect", CYAN), ("Tune", GREEN), ("Save", ORANGE)]
    for i, (label, col) in enumerate(cycle):
        x = MARGIN_X + 100 + i * 145
        node(c, x, y - 30, 100, 34, label, col)
        if i < len(cycle) - 1:
            arrow(c, x + 100, y - 47, x + 145, y - 47)
    footer(c, 12)


def page_rendering_resource(c: canvas.Canvas) -> None:
    y = title(c, "11", "RENDERING / RESOURCE", "描画・演出・リソース管理", "描画基盤、ゲーム演出、ロード処理を分け、見た目と開発効率の両方を扱いやすくした。")
    y -= 18
    col_w = (CONTENT_W - 24) / 3
    for i, (head, body, col) in enumerate([("OffscreenRenderManager", "一度オフスクリーンへ描画し、ポストエフェクトや ImGui Scene 表示へ利用。", BLUE), ("Post Effects", "Fullscreen、GrayScale、Vignette、BoxFilter、RadialBlur、Outline。", CYAN), ("Boss / Combat Presentation", "登場、撃破、雷撃などの演出を専用クラスで制御。", PURPLE), ("GameTexture / Model / Audio Cache", "重複ロードを抑え、利用側コードを単純化。", GREEN)]):
        x = MARGIN_X + (i % 3) * (col_w + 12)
        yy = y - (i // 3) * 82
        box(c, x, yy, col_w, 64, head, body, col, colors.white, 7.7)
    y -= 182
    text(c, MARGIN_X, y, "リソース管理の工夫", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, ["ロードパスを ResourcePaths / DataPaths に集約し、Resources 配下を一貫して扱う。", "SceneTransition と CurtainTransition により、画面切り替えと状態遷移を自然につなぐ。"], CONTENT_W, 8.8, 13)
    footer(c, 13)


def page_summary(c: canvas.Canvas) -> None:
    y = title(c, "12", "SUMMARY", "まとめ / 今後の改善", "DX12 の低レイヤ実装とゲーム固有ロジックをつなぎ、調整しやすい制作環境を作った。")
    y -= 18
    col_w = (CONTENT_W - 18) / 2
    for i, (head, body, col) in enumerate([("学び 1: エンジン設計", "Framework、Scene、描画、入力、音、リソースを分離し、ゲーム側が使う API を整理。", BLUE), ("学び 2: データ駆動", "CSV と ImGui によって、調整と実装を切り離す重要性を実感。", GREEN), ("学び 3: 最適化", "空間分割、上限管理、Telemetry で、大量オブジェクトの負荷を数値で判断。", ORANGE), ("学び 4: 完成優先", "演出・UI・結果画面まで含め、遊べる流れとして成立させる判断力を強化。", PURPLE)]):
        x = MARGIN_X + (i % 2) * (col_w + 18)
        yy = y - (i // 2) * 80
        box(c, x, yy, col_w, 62, head, body, col, colors.white, 7.8)
    y -= 188
    text(c, MARGIN_X, y, "Future Goal", 14, NAVY, FONT_BOLD)
    bullets(c, MARGIN_X, y - 26, ["タスクシステムや非同期ロードを導入し、DX12 の高度な機能活用へ広げる。", "調整値保存・再読込をさらに統一し、制作サイクルを短縮する。", "プレゼンテーション層を継続的に整理し、演出追加時の影響範囲を狭める。"], CONTENT_W, 8.8, 13)
    right_text(c, PAGE_W - MARGIN_X, 64, "THANK YOU", 18, NAVY, FONT_BOLD)
    footer(c, 14)


PAGES = [page_cover, page_index, page_production, page_focus, page_feature, page_arch, page_flow, page_player_weapon, page_collision, page_levelup, page_data, page_ui_debug, page_rendering_resource, page_summary]


def build() -> None:
    register_fonts()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(OUT_PDF), pagesize=landscape(A4))
    c.setTitle("プログラム説明資料 - KuboEngine")
    c.setAuthor("TAKU OKUBO")
    for i, page in enumerate(PAGES):
        page(c)
        if i != len(PAGES) - 1:
            c.showPage()
    c.save()
    print(OUT_PDF)


if __name__ == "__main__":
    build()
