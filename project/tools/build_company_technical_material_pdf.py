from __future__ import annotations

from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.pagesizes import A4, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "output" / "pdf"
OUT_PDF = OUT_DIR / "kuboengine_company_technical_material.pdf"

PAGE_W, PAGE_H = landscape(A4)
MARGIN = 34
TOP = PAGE_H - 30
BOTTOM = 30
CONTENT_W = PAGE_W - MARGIN * 2

FONT_REG = "BizUDGothicRegular"
FONT_BOLD = "BizUDGothicBold"
FONT_MONO = "Courier"

NAVY = colors.HexColor("#0B1220")
INK = colors.HexColor("#111827")
SUB = colors.HexColor("#475569")
LINE = colors.HexColor("#CBD5E1")
PAPER = colors.HexColor("#F8FAFC")
BLUE = colors.HexColor("#2563EB")
CYAN = colors.HexColor("#06B6D4")
GREEN = colors.HexColor("#10B981")
ORANGE = colors.HexColor("#F59E0B")
RED = colors.HexColor("#EF4444")
PURPLE = colors.HexColor("#7C3AED")


def register_fonts() -> None:
    pdfmetrics.registerFont(TTFont(FONT_REG, r"C:\Windows\Fonts\BIZ-UDGothicR.ttc"))
    pdfmetrics.registerFont(TTFont(FONT_BOLD, r"C:\Windows\Fonts\BIZ-UDGothicB.ttc"))


def sw(value: str, size: float, font: str = FONT_REG) -> float:
    return pdfmetrics.stringWidth(value, font, size)


def wrap(value: str, width: float, size: float, font: str = FONT_REG) -> list[str]:
    lines: list[str] = []
    for raw in value.splitlines():
        raw = raw.strip()
        if not raw:
            lines.append("")
            continue
        cur = ""
        for ch in raw:
            nxt = cur + ch
            if not cur or sw(nxt, size, font) <= width:
                cur = nxt
            else:
                lines.append(cur)
                cur = ch
        if cur:
            lines.append(cur)
    return lines


def text(c: canvas.Canvas, x: float, y: float, value: str, size: float = 9, color=INK, font: str = FONT_REG) -> None:
    c.setFont(font, size)
    c.setFillColor(color)
    c.drawString(x, y, value)


def paragraph(c: canvas.Canvas, x: float, y: float, value: str, width: float, size: float = 8.2, leading: float = 11.2, color=INK) -> float:
    for line in wrap(value, width, size):
        text(c, x, y, line, size, color)
        y -= leading
    return y


def footer(c: canvas.Canvas, page: int) -> None:
    c.setStrokeColor(LINE)
    c.line(MARGIN, 22, PAGE_W - MARGIN, 22)
    text(c, MARGIN, 10, "KuboEngine / Company Technical Material", 6.8, SUB)
    c.drawRightString(PAGE_W - MARGIN, 10, f"{page:02d}")


def header(c: canvas.Canvas, page: int, section: str, title: str, subtitle: str) -> float:
    text(c, MARGIN, TOP, f"{page:02d} / {section}", 7.5, BLUE, FONT_BOLD)
    text(c, MARGIN, TOP - 28, title, 22, NAVY, FONT_BOLD)
    paragraph(c, MARGIN, TOP - 48, subtitle, CONTENT_W, 8.5, 11.5, SUB)
    return TOP - 74


def box(c: canvas.Canvas, x: float, y: float, w: float, h: float, title: str, body: str, accent=BLUE, fill=colors.white) -> None:
    c.setFillColor(fill)
    c.setStrokeColor(LINE)
    c.roundRect(x, y - h, w, h, 5, stroke=1, fill=1)
    c.setFillColor(accent)
    c.rect(x, y - 4, w, 4, stroke=0, fill=1)
    text(c, x + 10, y - 20, title, 9.5, INK, FONT_BOLD)
    paragraph(c, x + 10, y - 36, body, w - 20, 7.3, 10.2, INK)


def node(c: canvas.Canvas, x: float, y: float, w: float, h: float, label: str, fill=BLUE, color=colors.white) -> None:
    c.setFillColor(fill)
    c.roundRect(x, y - h, w, h, 6, stroke=0, fill=1)
    lines = wrap(label, w - 12, 8.1, FONT_BOLD)
    yy = y - h / 2 + (len(lines) - 1) * 5
    for line in lines:
        text(c, x + (w - sw(line, 8.1, FONT_BOLD)) / 2, yy, line, 8.1, color, FONT_BOLD)
        yy -= 10


def arrow(c: canvas.Canvas, x1: float, y1: float, x2: float, y2: float) -> None:
    c.setStrokeColor(LINE)
    c.setLineWidth(1.4)
    c.line(x1, y1, x2, y2)
    dx = 1 if x2 >= x1 else -1
    c.line(x2, y2, x2 - 5 * dx, y2 + 3)
    c.line(x2, y2, x2 - 5 * dx, y2 - 3)


def bullets(c: canvas.Canvas, x: float, y: float, items: list[str], width: float, color=BLUE) -> None:
    for item in items:
        c.setFillColor(color)
        c.circle(x + 3, y + 3, 2, stroke=0, fill=1)
        y = paragraph(c, x + 13, y, item, width - 13, 7.8, 10.8)
        y -= 2


def cover(c: canvas.Canvas) -> None:
    c.setFillColor(NAVY)
    c.rect(0, 0, PAGE_W, PAGE_H, stroke=0, fill=1)
    text(c, MARGIN, PAGE_H - 72, "TECHNICAL MATERIAL", 9, CYAN, FONT_BOLD)
    text(c, MARGIN, PAGE_H - 128, "KuboEngine", 34, colors.white, FONT_BOLD)
    text(c, MARGIN, PAGE_H - 160, "DirectXGame / 3D Survival Shooting", 14, colors.HexColor("#E2E8F0"), FONT_BOLD)
    paragraph(c, MARGIN, PAGE_H - 198, "DirectX 12 と C++20 で制作した自作エンジンとゲーム実装。設計、低レイヤリソース管理、データ駆動、検証基盤を中心に説明します。", 420, 9.5, 14, colors.white)
    x0 = PAGE_W - 360
    y0 = PAGE_H - 112
    node(c, x0, y0, 130, 42, "engine/\nFramework + DX12", BLUE)
    node(c, x0 + 190, y0, 130, 42, "game/directxgame\nScenes + Gameplay", GREEN)
    arrow(c, x0 + 130, y0 - 21, x0 + 190, y0 - 21)
    node(c, x0 + 90, y0 - 120, 160, 42, "Resources/data\nCSV Tuning", ORANGE)
    arrow(c, x0 + 255, y0 - 42, x0 + 200, y0 - 120)
    x = MARGIN
    for label, col in [("C++20", BLUE), ("DirectX 12", CYAN), ("HLSL", GREEN), ("CSV Driven", ORANGE), ("Tests", PURPLE)]:
        c.setFillColor(colors.HexColor("#101A2D"))
        c.setStrokeColor(col)
        c.setLineWidth(1.6)
        c.roundRect(x, 46, 94, 32, 4, stroke=1, fill=1)
        text(c, x + (94 - sw(label, 8.0, FONT_BOLD)) / 2, 58, label, 8.0, colors.white, FONT_BOLD)
        x += 104
    footer(c, 1)


def toc(c: canvas.Canvas) -> None:
    y = header(c, 2, "CONTENTS", "目次", "採用担当が見たい規模、担当範囲、技術力、問題解決を順番に追える構成です。")
    items = ["作品概要", "制作体制", "開発スケジュール", "担当範囲", "全体アーキテクチャ", "エンジン構成", "ゲーム構成", "各技術要素", "パフォーマンス改善", "苦労したこと", "学んだこと", "今後の改善", "まとめ"]
    x = MARGIN
    step = (CONTENT_W - 90) / 6
    for i, item in enumerate(items, start=3):
        col = [BLUE, CYAN, GREEN, ORANGE, PURPLE, RED][(i - 3) % 6]
        px = x + ((i - 3) % 7) * step
        py = y - ((i - 3) // 7) * 95
        node(c, px, py, 86, 36, f"{i:02d}\n{item}", col)
        if (i - 3) % 7 != 6 and i != 15:
            arrow(c, px + 86, py - 18, px + step, py - 18)
    footer(c, 2)


def loop_page(c: canvas.Canvas) -> None:
    y = header(c, 3, "OVERVIEW", "作品概要", "敵を倒して EXP を集め、レベルアップで武器や能力を強化していく 3D サバイバルシューティングです。")
    labels = [("Enemy", RED), ("EXP", GREEN), ("LevelUp", BLUE), ("Weapon Build", PURPLE), ("Boss", ORANGE)]
    x0 = MARGIN + 52
    for i, (label, col) in enumerate(labels):
        x = x0 + i * 140
        node(c, x, y - 30, 104, 42, label, col)
        if i < len(labels) - 1:
            arrow(c, x + 104, y - 51, x + 140, y - 51)
    paragraph(c, MARGIN, y - 130, "目的は、ゲームとして遊べる流れを作りながら、DirectX 12 の描画基盤、scene管理、データ調整、負荷対策まで自分で設計することです。", CONTENT_W, 9.0, 13)
    footer(c, 3)


def responsibility_page(c: canvas.Canvas, page: int, title: str, subtitle: str, cards: list[tuple[str, str, object]]) -> None:
    y = header(c, page, "SCOPE", title, subtitle)
    w = (CONTENT_W - 32) / 4
    for i, (head, body, col) in enumerate(cards):
        box(c, MARGIN + (i % 4) * (w + 10.5), y - (i // 4) * 92, w, 72, head, body, col, PAPER)
    footer(c, page)


def timeline_page(c: canvas.Canvas) -> None:
    y = header(c, 5, "SCHEDULE", "開発スケジュール", "開発は、描画基盤からゲーム実装、データ駆動、リファクタリング、検証へ段階的に進めました。")
    phases = [("基盤", "DX12 / Framework", BLUE), ("ゲーム", "Scene / Player / Enemy", GREEN), ("調整", "CSV / Debug UI", ORANGE), ("改善", "Frame / SRV / Cache", PURPLE), ("検証", "CPU tests / Stress", RED)]
    x0 = MARGIN + 35
    for i, (head, body, col) in enumerate(phases):
        x = x0 + i * 145
        node(c, x, y - 30, 104, 38, head, col)
        paragraph(c, x - 6, y - 82, body, 116, 7.5, 10, SUB)
        if i < len(phases) - 1:
            arrow(c, x + 104, y - 49, x + 145, y - 49)
    footer(c, 5)


def architecture_page(c: canvas.Canvas) -> None:
    y = header(c, 7, "ARCHITECTURE", "全体アーキテクチャ", "`main.cpp` は起動処理に絞り、GameSceneFactory に scene 生成を委譲します。")
    node(c, MARGIN + 20, y, 110, 42, "main.cpp", NAVY)
    node(c, MARGIN + 185, y, 120, 42, "Engine::Scene::Game", BLUE)
    node(c, MARGIN + 365, y, 124, 42, "GameSceneFactory", CYAN)
    arrow(c, MARGIN + 130, y - 21, MARGIN + 185, y - 21)
    arrow(c, MARGIN + 305, y - 21, MARGIN + 365, y - 21)
    for x, label, col in [(MARGIN + 130, "TitleScene", GREEN), (MARGIN + 310, "PlayScene", ORANGE), (MARGIN + 490, "ResultScene", PURPLE)]:
        node(c, x, y - 120, 118, 38, label, col)
        arrow(c, MARGIN + 427, y - 42, x + 59, y - 120)
    box(c, PAGE_W - MARGIN - 250, y - 190, 250, 82, "設計意図", "起動、共通基盤、ゲーム固有sceneの変更理由を分ける。TitleやResultを差し替えてもFrameworkに影響しない構成にした。", BLUE, colors.white)
    footer(c, 7)


def engine_page(c: canvas.Canvas) -> None:
    y = header(c, 8, "ENGINE", "エンジン構成", "`engine/` は DirectX、input、audio、resource、particle、camera、scene infrastructure を担当します。")
    cx = PAGE_W / 2 - 60
    node(c, cx, y - 30, 120, 46, "Framework", NAVY)
    subs = [("DirectXCommon", BLUE), ("Input", CYAN), ("Audio", GREEN), ("Texture / Model", ORANGE), ("Particle", PURPLE), ("Camera", RED)]
    positions = [(MARGIN + 30, y), (MARGIN + 185, y - 110), (MARGIN + 350, y - 150), (MARGIN + 515, y - 110), (MARGIN + 640, y), (MARGIN + 350, y + 20)]
    for (label, col), (x, yy) in zip(subs, positions):
        node(c, x, yy, 118, 36, label, col)
        arrow(c, cx + 60, y - 53, x + 59, yy - 36)
    footer(c, 8)


def game_page(c: canvas.Canvas) -> None:
    y = header(c, 9, "GAME", "ゲーム構成", "`PlayScene` をcomposition rootにし、進行、player、enemy、HUD、effect、debugへ分割しました。")
    node(c, MARGIN + 20, y - 20, 120, 44, "PlayScene", NAVY)
    cards = [("GameplayFlow", BLUE), ("PlayerManager", GREEN), ("EnemyManager", RED), ("HUD", CYAN), ("Effects", PURPLE), ("DebugUI", ORANGE)]
    for i, (label, col) in enumerate(cards):
        x = MARGIN + 205 + (i % 3) * 150
        yy = y - (i // 3) * 82
        node(c, x, yy, 118, 36, label, col)
        arrow(c, MARGIN + 140, y - 42, x, yy - 18)
    paragraph(c, MARGIN, y - 190, "大きなscene classへ処理を集めず、変更理由が違う処理を分離しました。ECSのような大きな仕組みではなく、個人制作で追いやすい責務分離にしています。", CONTENT_W, 8.8, 13)
    footer(c, 9)


def tech_page(c: canvas.Canvas) -> None:
    y = header(c, 10, "TECH", "各技術要素", "設計、描画、データ、衝突、検証を技術要素として整理しました。")
    cards = [
        ("FrameContext", "2 frame分のallocator、upload arena、fence、deferred releaseを保持。", BLUE),
        ("SrvManager", "SRV heapを単独所有し、allocate/freeとhigh-watermarkを記録。", CYAN),
        ("CSV Driven", "武器、敵、UI、level-up、resourceをCSVで調整。", GREEN),
        ("OBB + Spatial", "敵や弾の候補を近傍に絞り、回転を含む判定を扱う。", ORANGE),
        ("State Flow", "Start、Playing、Boss、Pause、LevelUp、Deadを明示。", PURPLE),
    ]
    w = (CONTENT_W - 48) / 5
    for i, (head, body, col) in enumerate(cards):
        box(c, MARGIN + i * (w + 12), y, w, 150, head, body, col, PAPER)
    footer(c, 10)


def performance_page(c: canvas.Canvas) -> None:
    y = header(c, 11, "PERFORMANCE", "パフォーマンス改善", "未計測のFPS値は使わず、負荷が増える箇所に対して設計上行った対策を説明します。")
    flows = [("Problem", "GPU resource lifetime\ncollision candidates\ndescriptor growth", RED), ("Approach", "FrameContext\nspatial map\nSRV telemetry", BLUE), ("Proof", "CPU tests\nscene stress\nSRV 105/105", GREEN)]
    for i, (head, body, col) in enumerate(flows):
        x = MARGIN + 70 + i * 230
        box(c, x, y, 170, 120, head, body, col, colors.white)
        if i < 2:
            arrow(c, x + 170, y - 60, x + 230, y - 60)
    footer(c, 11)


def hardship_page(c: canvas.Canvas) -> None:
    y = header(c, 12, "PROBLEM SOLVING", "苦労したこと", "動いた後に見つかる危険箇所を、原因と修正方針に分けて整理しました。")
    rows = [
        ("GPU寿命", "GPU参照中のupload resourceを早く解放する危険", "FrameContextとdeferred release"),
        ("Scene遷移", "descriptorやtextureが増え続ける危険", "scene stressとSRV high-watermark"),
        ("調整値", "武器や敵の値がコードに散らばる", "CSV化とstrict parse"),
    ]
    w = (CONTENT_W - 24) / 3
    for i, (head, problem, fix) in enumerate(rows):
        box(c, MARGIN + i * (w + 12), y, w, 142, head, f"問題: {problem}\n対応: {fix}", [RED, ORANGE, BLUE][i], PAPER)
    footer(c, 12)


def learning_page(c: canvas.Canvas) -> None:
    y = header(c, 13, "LEARNING", "学んだこと", "描画APIを使うだけでなく、所有権、調整、検証まで設計する必要があると学びました。")
    node(c, PAGE_W / 2 - 65, y, 130, 42, "KuboEngine", NAVY)
    for x, yy, label, col in [
        (MARGIN + 90, y - 80, "低レイヤ\nresource lifetime", BLUE),
        (PAGE_W / 2 - 65, y - 160, "設計\nresponsibility", GREEN),
        (PAGE_W - MARGIN - 220, y - 80, "検証\nstress + tests", ORANGE),
    ]:
        node(c, x, yy, 130, 44, label, col)
        arrow(c, PAGE_W / 2, y - 42, x + 65, yy)
    footer(c, 13)


def future_page(c: canvas.Canvas) -> None:
    y = header(c, 14, "ROADMAP", "今後の改善", "現在の設計を土台に、計測と非同期処理を強化します。")
    items = [("PIX計測", "GPU pass / draw call", BLUE), ("Shadow確認", "silhouette / timing", CYAN), ("Async Load", "runtime load待ち削減", GREEN), ("Typed Handle", "owner明確化", ORANGE), ("CSV Test", "schema / fixture", PURPLE)]
    x0 = MARGIN + 35
    for i, (head, body, col) in enumerate(items):
        x = x0 + i * 145
        box(c, x, y, 112, 92, head, body, col, PAPER)
        if i < len(items) - 1:
            arrow(c, x + 112, y - 46, x + 145, y - 46)
    footer(c, 14)


def summary_page(c: canvas.Canvas) -> None:
    y = header(c, 15, "SUMMARY", "まとめ", "KuboEngineは、技術、設計、問題解決、実装、検証を一通り扱った個人制作です。")
    node(c, PAGE_W / 2 - 80, y - 45, 160, 54, "KuboEngine\nDirectXGame", NAVY)
    items = [("技術力", "DX12 / HLSL / C++20", BLUE), ("設計力", "engine/game分離", GREEN), ("問題解決", "resource / collision", ORANGE), ("実装力", "gameplay / CSV / tests", PURPLE)]
    for x, yy, (head, body, col) in [
        (MARGIN + 80, y, items[0]),
        (PAGE_W - MARGIN - 230, y, items[1]),
        (MARGIN + 80, y - 150, items[2]),
        (PAGE_W - MARGIN - 230, y - 150, items[3]),
    ]:
        box(c, x, yy, 150, 72, head, body, col, PAPER)
        arrow(c, PAGE_W / 2, y - 72, x + 75, yy - 72)
    footer(c, 15)


def build() -> None:
    register_fonts()
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    c = canvas.Canvas(str(OUT_PDF), pagesize=landscape(A4))
    c.setTitle("KuboEngine 企業提出向け技術資料")
    c.setAuthor("TAKU OKUBO")
    pages = [
        cover,
        toc,
        loop_page,
        lambda cnv: responsibility_page(cnv, 4, "制作体制", "個人制作として、engine、game、data、tools、testsまで一貫して担当しました。", [
            ("engine/", "Framework、DirectX、input、audio、resource管理。", BLUE),
            ("game/", "scene、player、enemy、weapon、HUD、effect。", GREEN),
            ("Resources/data", "武器、敵、UI、level-up、resource manifest。", ORANGE),
            ("tools/tests", "CPU regression、scene stress、PIX preflight。", PURPLE),
        ]),
        timeline_page,
        lambda cnv: responsibility_page(cnv, 6, "担当範囲", "コード全般を担当し、ゲーム実装だけでなく調整・検証の仕組みも作成しました。", [
            ("DirectX基盤", "frame resource、SRV、pipeline、shader compile。", BLUE),
            ("Gameplay", "player、enemy、weapon、level-up、boss。", GREEN),
            ("UI / Debug", "HUD、mini map、debug panel、freeze操作。", CYAN),
            ("Data", "CSV tuning、resource manifest、strict parse。", ORANGE),
            ("Tests", "CPU test project、scene transition stress。", PURPLE),
        ]),
        architecture_page,
        engine_page,
        game_page,
        tech_page,
        performance_page,
        hardship_page,
        learning_page,
        future_page,
        summary_page,
    ]
    for i, page in enumerate(pages):
        page(c)
        if i != len(pages) - 1:
            c.showPage()
    c.save()
    print(OUT_PDF)


if __name__ == "__main__":
    build()
