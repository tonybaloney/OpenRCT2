"""Package browser-rendered pixel slides as a two-slide, image-based PowerPoint."""

import argparse
import json
import struct
import xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape
from zipfile import ZIP_DEFLATED, ZipFile


def export(directory: Path, output: Path) -> None:
    if output.exists():
        raise FileExistsError(f"Choose a new PowerPoint filename: {output}")
    images = [directory / f"slide-{i}.png" for i in (1, 2)]
    for image in images:
        header = image.read_bytes()[:24]
        if header[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", header[16:24]) != (1280, 720):
            raise ValueError(f"Expected a 1280x720 PNG slide: {image}")
    data = json.loads((directory / "eval-data.json").read_text(encoding="utf-8"))
    a = "http://schemas.openxmlformats.org/drawingml/2006/main"
    p = "http://schemas.openxmlformats.org/presentationml/2006/main"
    r = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
    rel_ns = "http://schemas.openxmlformats.org/package/2006/relationships"
    group = (
        '<p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>'
        '<p:grpSpPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="0" cy="0"/>'
        '<a:chOff x="0" y="0"/><a:chExt cx="0" cy="0"/></a:xfrm></p:grpSpPr>'
    )

    def relationships(entries):
        root = ET.Element("Relationships", xmlns=rel_ns)
        for identifier, kind, target in entries:
            ET.SubElement(root, "Relationship", Id=identifier, Type=f"{r}/{kind}", Target=target)
        return ET.tostring(root, encoding="unicode")

    parts = {
        "_rels/.rels": relationships([("rId1", "officeDocument", "ppt/presentation.xml")]),
        "ppt/presentation.xml": (
            f'<p:presentation xmlns:a="{a}" xmlns:r="{r}" xmlns:p="{p}">'
            '<p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>'
            '<p:sldIdLst><p:sldId id="256" r:id="rId2"/><p:sldId id="257" r:id="rId3"/></p:sldIdLst>'
            '<p:sldSz cx="12192000" cy="6858000" type="screen16x9"/>'
            '<p:notesSz cx="6858000" cy="9144000"/></p:presentation>'
        ),
        "ppt/_rels/presentation.xml.rels": relationships([
            ("rId1", "slideMaster", "slideMasters/slideMaster1.xml"),
            ("rId2", "slide", "slides/slide1.xml"),
            ("rId3", "slide", "slides/slide2.xml"),
        ]),
        "ppt/slideMasters/slideMaster1.xml": (
            f'<p:sldMaster xmlns:a="{a}" xmlns:r="{r}" xmlns:p="{p}">'
            f'<p:cSld name="Pixel canvas"><p:spTree>{group}</p:spTree></p:cSld>'
            '<p:clrMap accent1="accent1" accent2="accent2" accent3="accent3" accent4="accent4" '
            'accent5="accent5" accent6="accent6" bg1="lt1" bg2="lt2" folHlink="folHlink" '
            'hlink="hlink" tx1="dk1" tx2="dk2"/>'
            '<p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst>'
            '<p:txStyles><p:titleStyle/><p:bodyStyle/><p:otherStyle/></p:txStyles></p:sldMaster>'
        ),
        "ppt/slideMasters/_rels/slideMaster1.xml.rels": relationships([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "theme", "../theme/theme1.xml"),
        ]),
        "ppt/slideLayouts/slideLayout1.xml": (
            f'<p:sldLayout xmlns:a="{a}" xmlns:r="{r}" xmlns:p="{p}" type="blank" preserve="1">'
            f'<p:cSld name="Blank"><p:spTree>{group}</p:spTree></p:cSld>'
            '<p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>'
        ),
        "ppt/slideLayouts/_rels/slideLayout1.xml.rels": relationships([
            ("rId1", "slideMaster", "../slideMasters/slideMaster1.xml"),
        ]),
    }
    colors = dict(dk1="252025", lt1="F3E8C6", dk2="294D35", lt2="D5C49C", accent1="603A74",
                  accent2="48B8F0", accent3="F4BD46", accent4="DE83ED", accent5="7ED175",
                  accent6="B5403D", hlink="3F2461", folHlink="603A74")
    scheme = "".join(f'<a:{name}><a:srgbClr val="{color}"/></a:{name}>' for name, color in colors.items())
    fill = '<a:solidFill><a:schemeClr val="phClr"/></a:solidFill>'
    line = f'<a:ln w="9525">{fill}<a:prstDash val="solid"/><a:miter lim="800000"/></a:ln>'
    font = '<a:latin typeface="Consolas"/><a:ea typeface=""/><a:cs typeface=""/>'
    parts["ppt/theme/theme1.xml"] = (
        f'<a:theme xmlns:a="{a}" name="D3 Park Lab"><a:themeElements>'
        f'<a:clrScheme name="Park Lab">{scheme}</a:clrScheme>'
        f'<a:fontScheme name="Pixel monospace"><a:majorFont>{font}</a:majorFont>'
        f'<a:minorFont>{font}</a:minorFont></a:fontScheme>'
        f'<a:fmtScheme name="Flat canvas"><a:fillStyleLst>{fill * 3}</a:fillStyleLst>'
        f'<a:lnStyleLst>{line * 3}</a:lnStyleLst>'
        f'<a:effectStyleLst>{"<a:effectStyle><a:effectLst/></a:effectStyle>" * 3}</a:effectStyleLst>'
        f'<a:bgFillStyleLst>{fill * 3}</a:bgFillStyleLst></a:fmtScheme>'
        '</a:themeElements></a:theme>'
    )
    descriptions = [
        "Original guest decision flow: needs and knowledge, native eligibility, native destination selection, "
        "persistent intent, bounded native pathfinding. Historical D3-Lite evidence shows incumbent-target "
        "confidence anchoring. Accepted unchanged destinations are not redirects.",
        "D3-assisted decision flow: frozen native shortlist, two-order classifier scoring, unchanged confidence "
        "gate, main-thread revalidation or native fallback, then native pathfinding. "
        + "; ".join(f"{run['label']}: {run['outcomes']['applied']} redirects, "
                    f"{run['outcomes']['low_confidence']} low-confidence fallbacks"
                    for run in data["runs"])
        + ". One park and one run per treatment; no statistical or long-term improvement claim.",
    ]
    for index, description in enumerate(descriptions, 1):
        parts[f"ppt/slides/slide{index}.xml"] = (
            f'<p:sld xmlns:a="{a}" xmlns:r="{r}" xmlns:p="{p}"><p:cSld><p:spTree>{group}'
            f'<p:pic><p:nvPicPr><p:cNvPr id="2" name="Park Lab slide {index}" '
            f'descr="{escape(description, {chr(34): "&quot;"})}"/>'
            '<p:cNvPicPr><a:picLocks noChangeAspect="1"/></p:cNvPicPr><p:nvPr/></p:nvPicPr>'
            '<p:blipFill><a:blip r:embed="rId2"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>'
            '<p:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="12192000" cy="6858000"/></a:xfrm>'
            '<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic>'
            '</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>'
        )
        parts[f"ppt/slides/_rels/slide{index}.xml.rels"] = relationships([
            ("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"),
            ("rId2", "image", f"../media/slide{index}.png"),
        ])
    types_ns = "http://schemas.openxmlformats.org/package/2006/content-types"
    types = ET.Element("Types", xmlns=types_ns)
    for extension, content in [("rels", "application/vnd.openxmlformats-package.relationships+xml"),
                               ("xml", "application/xml"), ("png", "image/png")]:
        ET.SubElement(types, "Default", Extension=extension, ContentType=content)
    content_types = {
        "ppt/presentation.xml": "presentation.main",
        "ppt/slideMasters/slideMaster1.xml": "slideMaster",
        "ppt/slideLayouts/slideLayout1.xml": "slideLayout",
        "ppt/theme/theme1.xml": "theme",
        "ppt/slides/slide1.xml": "slide",
        "ppt/slides/slide2.xml": "slide",
    }
    for name, kind in content_types.items():
        content = (f"application/vnd.openxmlformats-officedocument.{kind}+xml" if kind == "theme"
                   else f"application/vnd.openxmlformats-officedocument.presentationml.{kind}+xml")
        ET.SubElement(types, "Override", PartName=f"/{name}", ContentType=content)
    parts["[Content_Types].xml"] = ET.tostring(types, encoding="unicode")
    for content in parts.values():
        ET.fromstring(content)
    with ZipFile(output, "x", ZIP_DEFLATED) as archive:
        for name, content in parts.items():
            archive.writestr(name, '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>' + content)
        for index, image in enumerate(images, 1):
            archive.write(image, f"ppt/media/slide{index}.png")
    with ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError("PowerPoint ZIP verification failed.")
        presentation = ET.fromstring(archive.read("ppt/presentation.xml"))
        if len(presentation.find(f"{{{p}}}sldIdLst")) != 2:
            raise ValueError("Expected exactly two PowerPoint slides.")
    print(f"Verified two-slide image-based PowerPoint: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Report directory containing eval-data.json and slide PNGs")
    parser.add_argument("output", type=Path, help="New .pptx output filename")
    args = parser.parse_args()
    export(args.directory, args.output)
