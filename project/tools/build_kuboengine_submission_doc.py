from pathlib import Path

from docx import Document
from docx.enum.section import WD_SECTION
from docx.enum.table import WD_CELL_VERTICAL_ALIGNMENT, WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor


ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "output" / "documents"
DOCX_PATH = OUT_DIR / "KuboEngine_program_explanation_A4.docx"

FONT = "Yu Gothic"
NAVY = "15324A"
BLUE = "27678A"
GREEN = "39735D"
ORANGE = "B46A32"
PALE_BLUE = "EAF2F6"
PALE_GREEN = "EAF3EE"
PALE_ORANGE = "F8EFE7"
PALE_GRAY = "F3F5F6"
GRAY = "5D6870"
WHITE = "FFFFFF"
INK = "1C252B"
TABLE_WIDTH = 9800


def set_cell_shading(cell, fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = tc_pr.find(qn("w:shd"))
    if shd is None:
        shd = OxmlElement("w:shd")
        tc_pr.append(shd)
    shd.set(qn("w:fill"), fill)


def set_cell_margins(cell, top=70, start=110, bottom=70, end=110):
    tc = cell._tc
    tc_pr = tc.get_or_add_tcPr()
    tc_mar = tc_pr.first_child_found_in("w:tcMar")
    if tc_mar is None:
        tc_mar = OxmlElement("w:tcMar")
        tc_pr.append(tc_mar)
    for m, v in (("top", top), ("start", start), ("bottom", bottom), ("end", end)):
        node = tc_mar.find(qn(f"w:{m}"))
        if node is None:
            node = OxmlElement(f"w:{m}")
            tc_mar.append(node)
        node.set(qn("w:w"), str(v))
        node.set(qn("w:type"), "dxa")


def set_repeat_table_header(row):
    tr_pr = row._tr.get_or_add_trPr()
    tbl_header = OxmlElement("w:tblHeader")
    tbl_header.set(qn("w:val"), "true")
    tr_pr.append(tbl_header)


def set_table_geometry(table, widths):
    table.autofit = False
    table.alignment = WD_TABLE_ALIGNMENT.LEFT
    tbl_pr = table._tbl.tblPr
    tbl_w = tbl_pr.find(qn("w:tblW"))
    if tbl_w is None:
        tbl_w = OxmlElement("w:tblW")
        tbl_pr.append(tbl_w)
    tbl_w.set(qn("w:w"), str(sum(widths)))
    tbl_w.set(qn("w:type"), "dxa")
    tbl_ind = tbl_pr.find(qn("w:tblInd"))
    if tbl_ind is None:
        tbl_ind = OxmlElement("w:tblInd")
        tbl_pr.append(tbl_ind)
    tbl_ind.set(qn("w:w"), "110")
    tbl_ind.set(qn("w:type"), "dxa")
    grid = table._tbl.tblGrid
    for child in list(grid):
        grid.remove(child)
    for width in widths:
        col = OxmlElement("w:gridCol")
        col.set(qn("w:w"), str(width))
        grid.append(col)
    for row in table.rows:
        for idx, cell in enumerate(row.cells):
            tc_w = cell._tc.get_or_add_tcPr().find(qn("w:tcW"))
            if tc_w is None:
                tc_w = OxmlElement("w:tcW")
                cell._tc.get_or_add_tcPr().append(tc_w)
            tc_w.set(qn("w:w"), str(widths[idx]))
            tc_w.set(qn("w:type"), "dxa")
            set_cell_margins(cell)


def set_run(run, size=9.2, bold=False, color=INK, italic=False, font=FONT):
    run.font.name = font
    run._element.get_or_add_rPr().rFonts.set(qn("w:ascii"), font)
    run._element.get_or_add_rPr().rFonts.set(qn("w:hAnsi"), font)
    run._element.get_or_add_rPr().rFonts.set(qn("w:eastAsia"), font)
    run.font.size = Pt(size)
    run.bold = bold
    run.italic = italic
    run.font.color.rgb = RGBColor.from_string(color)


def add_text(p, text, **kwargs):
    run = p.add_run(text)
    set_run(run, **kwargs)
    return run


def style_paragraph(p, before=0, after=4, line=1.12, keep=False):
    pf = p.paragraph_format
    pf.space_before = Pt(before)
    pf.space_after = Pt(after)
    pf.line_spacing = line
    pf.keep_with_next = keep


def add_para(doc, text, size=9.2, color=INK, bold=False, after=4, line=1.12, align=None):
    p = doc.add_paragraph()
    style_paragraph(p, after=after, line=line)
    if align is not None:
        p.alignment = align
    add_text(p, text, size=size, color=color, bold=bold)
    return p


def add_heading(doc, text, level=1, color=BLUE):
    p = doc.add_paragraph(style=f"Heading {level}")
    style_paragraph(p, before=8 if level == 1 else 5, after=4, line=1.0, keep=True)
    add_text(p, text, size=14.5 if level == 1 else 11.2, bold=True, color=color)
    return p


def add_kicker(doc, text, color=BLUE):
    p = doc.add_paragraph()
    style_paragraph(p, after=2, line=1.0, keep=True)
    add_text(p, text.upper(), size=8.2, bold=True, color=color)
    return p


def add_bullet(doc, text, color=INK):
    p = doc.add_paragraph(style="List Bullet")
    style_paragraph(p, after=2.4, line=1.08)
    add_text(p, text, size=8.9, color=color)
    return p


def add_table(doc, headers, rows, widths, header_fill=PALE_BLUE, font_size=8.2):
    table = doc.add_table(rows=1, cols=len(headers))
    table.style = "Table Grid"
    for idx, text in enumerate(headers):
        cell = table.rows[0].cells[idx]
        set_cell_shading(cell, header_fill)
        cell.vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
        p = cell.paragraphs[0]
        style_paragraph(p, after=0, line=1.0)
        add_text(p, text, size=font_size, bold=True, color=NAVY)
    set_repeat_table_header(table.rows[0])
    for row_data in rows:
        cells = table.add_row().cells
        for idx, text in enumerate(row_data):
            cells[idx].vertical_alignment = WD_CELL_VERTICAL_ALIGNMENT.CENTER
            p = cells[idx].paragraphs[0]
            style_paragraph(p, after=0, line=1.05)
            add_text(p, text, size=font_size, color=INK, bold=(idx == 0 and len(headers) == 2))
    set_table_geometry(table, widths)
    return table


def add_callout(doc, label, text, fill=PALE_GRAY, accent=BLUE):
    table = doc.add_table(rows=1, cols=2)
    table.style = "Table Grid"
    set_cell_shading(table.cell(0, 0), accent)
    set_cell_shading(table.cell(0, 1), fill)
    p0 = table.cell(0, 0).paragraphs[0]
    style_paragraph(p0, after=0, line=1.0)
    p0.alignment = WD_ALIGN_PARAGRAPH.CENTER
    add_text(p0, label, size=8.2, bold=True, color=WHITE)
    p1 = table.cell(0, 1).paragraphs[0]
    style_paragraph(p1, after=0, line=1.08)
    add_text(p1, text, size=8.6, color=INK)
    set_table_geometry(table, [1500, 8300])
    return table


def add_flow(doc, items, fill=PALE_BLUE, accent=BLUE):
    table = doc.add_table(rows=1, cols=len(items))
    table.style = "Table Grid"
    widths = [TABLE_WIDTH // len(items)] * len(items)
    widths[-1] += TABLE_WIDTH - sum(widths)
    for idx, item in enumerate(items):
        cell = table.cell(0, idx)
        set_cell_shading(cell, fill if idx % 2 == 0 else WHITE)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        style_paragraph(p, after=0, line=1.05)
        add_text(p, item, size=8.2, bold=True, color=accent)
    set_table_geometry(table, widths)


def add_page_header(section):
    header = section.header
    p = header.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    style_paragraph(p, after=0, line=1.0)
    add_text(p, "KuboEngine / DirectXGame  |  PROGRAM EXPLANATION", size=7.5, color=GRAY, bold=True)
    footer = section.footer
    fp = footer.paragraphs[0]
    fp.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    style_paragraph(fp, after=0, line=1.0)
    add_text(fp, "KuboEngine  |  ", size=7.3, color=GRAY)
    fld = OxmlElement("w:fldSimple")
    fld.set(qn("w:instr"), "PAGE")
    fp._p.append(fld)


def page_break(doc):
    doc.add_page_break()


def build():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    doc = Document()
    section = doc.sections[0]
    section.page_width = Cm(21.0)
    section.page_height = Cm(29.7)
    section.top_margin = Cm(1.45)
    section.bottom_margin = Cm(1.35)
    section.left_margin = Cm(1.65)
    section.right_margin = Cm(1.65)
    section.header_distance = Cm(0.65)
    section.footer_distance = Cm(0.6)
    add_page_header(section)

    normal = doc.styles["Normal"]
    normal.font.name = FONT
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), FONT)
    normal.font.size = Pt(9.2)
    normal.font.color.rgb = RGBColor.from_string(INK)
    for style_name in ("Heading 1", "Heading 2", "Heading 3"):
        style = doc.styles[style_name]
        style.font.name = FONT
        style._element.rPr.rFonts.set(qn("w:eastAsia"), FONT)
    lb = doc.styles["List Bullet"]
    lb.font.name = FONT
    lb._element.rPr.rFonts.set(qn("w:eastAsia"), FONT)
    lb.paragraph_format.left_indent = Cm(0.7)
    lb.paragraph_format.first_line_indent = Cm(-0.35)

    # Page 1
    add_kicker(doc, "PROGRAM EXPLANATION  |  WINDOWS / DIRECTX 12", ORANGE)
    p = doc.add_paragraph()
    style_paragraph(p, after=3, line=0.95, keep=True)
    add_text(p, "KuboEngine", size=25, bold=True, color=NAVY)
    p = doc.add_paragraph()
    style_paragraph(p, after=7, line=1.0, keep=True)
    add_text(p, "自作エンジン上で制作した3Dサバイバルアクション", size=13.5, bold=True, color=BLUE)
    add_para(doc, "C++20とDirectX 12でゲームエンジンを構築し、その上でTitle・Play・Resultまでを持つゲームを制作しました。描画基盤だけでなく、ゲーム進行、武器、敵、衝突判定、データ調整、デバッグ・検証まで一つのプロジェクトとして実装しています。", size=9.6, after=7, line=1.18)
    add_flow(doc, ["C++20", "DirectX 12", "HLSL", "ImGui", "CSV", "Assimp"])
    add_heading(doc, "作品概要", 1, NAVY)
    add_para(doc, "敵を倒して経験値を集め、レベルアップ時に武器や能力を選びながらボス戦まで生き残るゲームです。プレイ中に得たコインはResultで集計し、Titleで恒久強化やキャラクター解放に使用します。", after=5)
    add_table(doc, ["項目", "内容"], [
        ("ゲーム構成", "Title / Play / Result、通常戦闘、レベルアップ、ボス戦、恒久強化"),
        ("エンジン", "描画、入力、音声、モデル、スプライト、パーティクル、カメラ、シーン管理"),
        ("ゲーム実装", "Player、Enemy、Weapon、HUD、GameSession、デバッグ表示"),
        ("検証", "CPU回帰テスト、シーン遷移ストレス、SRVテレメトリ"),
    ], [2100, 7700], PALE_BLUE, 8.3)
    add_heading(doc, "技術的な軸", 1, NAVY)
    add_callout(doc, "設計", "engine/ と game/directxgame/ を分離し、PlaySceneから進行・武器・敵・HUDを責務ごとに切り出しました。", PALE_BLUE, BLUE)
    add_callout(doc, "描画", "FrameContext、Fence、SRV再利用、ResourceBarrierまでDirectX 12の寿命と状態を明示的に管理します。", PALE_GREEN, GREEN)
    add_callout(doc, "負荷対策", "OBBの前に空間分割で候補を絞り、上限値とテレメトリを組み合わせて破綻箇所を確認します。", PALE_ORANGE, ORANGE)

    # Page 2
    page_break(doc)
    add_kicker(doc, "01  ARCHITECTURE", BLUE)
    add_heading(doc, "エンジン層とゲーム層の分離", 1, NAVY)
    add_para(doc, "共通機能は engine/、作品固有の処理は game/directxgame/ に配置しています。起動処理では GameSceneFactory を Game へ渡し、main.cppは起動と例外処理に絞りました。ゲーム固有のシーンを共通基盤から直接生成しない境界を置いています。", after=5)
    add_flow(doc, ["main.cpp", "Game", "GameSceneFactory", "Title / Play / Result"], PALE_BLUE, BLUE)
    add_heading(doc, "PlaySceneを構成の起点にする", 2, BLUE)
    add_para(doc, "PlaySceneは各機能を保持して接続しますが、処理そのものは責務ごとのクラスへ分けています。変更理由が異なる処理を同じクラスへ集めないことで、武器追加やUI修正がシーン全体へ波及しにくい構成にしました。", after=4)
    add_table(doc, ["クラス", "役割"], [
        ("Framework", "ゲームループと共通サービス"),
        ("GameSceneFactory", "Title、Play、Resultの生成"),
        ("PlayScene", "ゲームプレイを構成する機能の保持と接続"),
        ("GameplayFlowController", "Start、Playing、Boss、Pause、LevelUp、Deadの状態管理"),
        ("PlayerManager", "HP、経験値、能力値、武器強化の窓口"),
        ("EnemyManager", "敵生成、EXP、衝突、ボス進行"),
        ("GameSession", "結果、コイン、恒久強化、キャラクター情報"),
    ], [2850, 6950], PALE_BLUE, 8.15)
    add_heading(doc, "ゲーム進行とシーン間データ", 2, BLUE)
    add_para(doc, "GameplayFlowControllerは通常更新、ボス演出、ポーズ、レベルアップ、死亡を状態として分離します。シーンをまたぐ値はGameSessionに集約し、RunResultへ経験値、コイン、最終レベル、撃破数、経過フレームを記録します。各シーンが互いの内部状態を直接参照しないよう、共有データの置き場所を限定しました。", after=4)
    add_flow(doc, ["Play\n結果を記録", "Result\n集計・表示", "Title\n強化・解放", "Play\n次のラン"], PALE_GREEN, GREEN)
    add_heading(doc, "状態に応じたレベルアップ候補", 2, GREEN)
    add_para(doc, "LevelUpChoiceServiceはPlayerManagerの状態から武器候補と能力候補を生成します。最大レベルの武器、上限到達済みの能力、HP満タン時の回復を除外し、武器と能力が混ざるよう候補数を調整してから表示順をシャッフルします。", after=3)
    add_callout(doc, "5武器", "通常弾 / 周囲弾 / 雷撃 / 爆発弾 / ソード。強化値はweaponUpgradeSettings.csvから読み込みます。", PALE_GREEN, GREEN)

    # Page 3
    page_break(doc)
    add_kicker(doc, "02  IMPLEMENTATION", GREEN)
    add_heading(doc, "DirectX 12のフレーム・リソース管理", 1, NAVY)
    add_para(doc, "CPUが次のフレームを準備している間もGPUが前のフレームを参照できるよう、2つのFrameContextを用意しました。各コンテキストはCommandAllocator、Upload Arena、遅延解放リソース、Fence値を保持します。GPUが参照する一時リソースをFence完了まで保持し、CPU側の早すぎる再利用を防ぎます。", after=4)
    add_flow(doc, ["Frame 0\nUpload / Fence", "GPU実行", "Frame 1\nUpload / Fence", "完了後に再利用"], PALE_GREEN, GREEN)
    add_heading(doc, "ディスクリプタとリソース状態", 2, GREEN)
    add_para(doc, "SrvManagerはTexture2D、CubeMap、StructuredBuffer、ShadowMapなどの用途と使用数を記録し、解放されたディスクリプタをFence完了後に再利用します。DirectXCommonはリソース状態を追跡し、遷移元と遷移先からResourceBarrierを発行します。追跡値と指定した遷移元が異なる場合は例外として検出します。", after=4)
    add_table(doc, ["管理対象", "実装"], [
        ("フレーム寿命", "FrameContext / Upload Arena / deferred release / Fence"),
        ("SRV寿命", "retired descriptorをFence完了後に再利用"),
        ("状態遷移", "全体・個別サブリソースの追跡とResourceBarrier"),
        ("追加バリア", "UAV barrier / aliasing barrier"),
    ], [2600, 7200], PALE_GREEN, 8.15)
    add_heading(doc, "Shadow Mapとオフスクリーン描画", 2, GREEN)
    add_para(doc, "Shadow Passではライト視点の深度マップを作成し、通常描画のPixel ShaderでSampleCmpLevelZeroによる3×3比較サンプリングを行います。開始時に深度テクスチャをDEPTH_WRITE、終了時にPIXEL_SHADER_RESOURCEへ遷移します。casterはライト範囲でカリングし、候補数・描画数・除外数を記録します。", after=4)
    add_flow(doc, ["PIXEL_SHADER_RESOURCE", "DEPTH_WRITE\nShadow Pass", "PIXEL_SHADER_RESOURCE", "通常描画で参照"], PALE_ORANGE, ORANGE)
    add_heading(doc, "OBBと空間分割", 2, ORANGE)
    add_para(doc, "通常弾、周囲弾、爆発弾、プレイヤー、敵の詳細判定にはOBBを使用します。敵は衝突半径が重なるすべてのXZセルへ登録し、検索後に重複を除去してからOBB判定へ進みます。セル境界で候補が漏れることを防ぎつつ、全敵との総当たりを避けます。", after=3)
    add_flow(doc, ["敵をセル登録", "周辺セル検索", "重複を除去", "OBB詳細判定", "ダメージ / 押し戻し"], PALE_ORANGE, ORANGE)
    add_callout(doc, "計測条件", "空間分割とShadow Passの性能値は未計測です。資料では実装と検証手段を説明し、FPSやGPU時間の改善値は断定しません。", PALE_ORANGE, ORANGE)

    # Page 4
    page_break(doc)
    add_kicker(doc, "03  VERIFICATION & NEXT", ORANGE)
    add_heading(doc, "調整と検証を実装の一部にする", 1, NAVY)
    add_para(doc, "プレイヤー能力、武器強化、敵設定、スポーン、UI配置、デバッグ調整値をCSVへ分離しました。数値変換では文字列全体を消費したか、有限値か、範囲内かを確認します。ImGuiでは敵数、弾数、パーティクル数、FPS、SRV使用数、Shadow Pass統計、衝突形状などを実行中に確認できます。", after=5)
    add_table(doc, ["確認方法", "対象", "結果・用途"], [
        ("CPU回帰テスト", "セルキー、HP、CSV、武器間隔、乱数、SoundHandle", "Debug / Release PASS"),
        ("Scene Stress", "Title → Play → Resultを3周", "各シーン3回訪問"),
        ("SRV telemetry", "使用数 / High-watermark", "105 / 105"),
        ("Debug x64 build", "修正後の状態遷移実装を含む", "2026-07-05 PASS"),
    ], [2100, 4800, 2900], PALE_BLUE, 7.9)
    add_heading(doc, "制作中に見直した点", 1, NAVY)
    add_table(doc, ["対象", "現在の構成"], [
        ("シーン生成", "GameSceneFactoryへ分離"),
        ("ゲーム進行", "GameplayFlowControllerへ分離"),
        ("シーン間データ", "GameSessionへ集約"),
        ("武器処理", "PlayerWeaponControllerへ分離"),
        ("衝突候補", "空間分割で絞り、OBBで詳細判定"),
        ("GPUリソース", "FrameContext、Fence、状態追跡、ResourceBarrier"),
        ("確認方法", "CPU回帰テストとシーン遷移ストレス"),
    ], [2850, 6950], PALE_GREEN, 8.05)
    add_heading(doc, "今後の改善", 1, ORANGE)
    add_bullet(doc, "PIXでShadow PassのGPU時間、フレーム時間、描画数を同じ条件で記録する。")
    add_bullet(doc, "敵数・弾数を固定した比較で、空間分割が有効になる条件をCPU時間として測定する。")
    add_bullet(doc, "起動後に追加したモデルをパス索引へ反映する更新処理と、非同期ロードを検討する。")
    add_bullet(doc, "管理クラスごとに異なるIDを取り違えないよう、型付きハンドルを広げる。")
    add_bullet(doc, "CSVスキーマとゲーム進行をGPUなしで確認できるfixtureを増やす。")
    add_callout(doc, "現在の制約", "Windows / DirectX 12専用です。Shadow Mapの状態遷移は実装済みですが、シルエットの最終確認とPIXによるGPU時間の定量評価は今後の検証項目です。", PALE_ORANGE, ORANGE)

    props = doc.core_properties
    props.title = "KuboEngine プログラム説明資料"
    props.subject = "C++ / DirectX 12 自作ゲームエンジンと3Dサバイバルアクション"
    props.author = "KuboEngine Developer"
    props.keywords = "C++, DirectX 12, HLSL, Game Engine, Portfolio"
    doc.save(DOCX_PATH)
    print(DOCX_PATH)


if __name__ == "__main__":
    build()
