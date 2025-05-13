import argparse
import os
import shutil

import os
import shutil
import zipfile
import tempfile
from lxml import etree

def remove_images_from_xps(input_xps_path: str, output_xps_folder) -> None:
    """
    Remove all <Image> elements from an XPS document
    and delete any image files under Resources/Images.

    :param input_xps_path:  Path to the source .xps file
    :param output_xps_path: Path where the cleaned .xps will be written
    """
    print(f"Working on {input_xps_path}")

    file_name = os.path.basename(input_xps_path)
    output_xps_path = os.path.join(output_xps_folder, file_name)
    with tempfile.TemporaryDirectory() as tmpdir:
        # 1. Unzip the XPS into a temp directory
        with zipfile.ZipFile(input_xps_path, 'r') as zin:
            zin.extractall(tmpdir)

        # 2. Remove <Image> elements from all XML pages
        ns = {'x': 'http://schemas.microsoft.com/xps/2005/06'}
        for root, _, files in os.walk(tmpdir):
            for fname in files:
                if fname.lower().endswith(('.fpage', '.xml')):
                    fullpath = os.path.join(root, fname)
                    try:
                        parser = etree.XMLParser(remove_blank_text=True)
                        tree = etree.parse(fullpath, parser)
                        changed = False

                        for img in tree.xpath('//x:Image', namespaces=ns):
                            img.getparent().remove(img)
                            changed = True
                        
                        for font_elem in tree.xpath('//x:Font|//x:FontResource', namespaces=ns):
                            font_elem.getparent().remove(font_elem)
                            changed = True

                        for glyph in tree.xpath('//x:Glyphs', namespaces=ns):
                            if 'FontUri' in glyph.attrib:
                                del glyph.attrib['FontUri']
                                changed = True

                        if changed:
                            tree.write(fullpath,
                                       xml_declaration=True,
                                       encoding='utf-8',
                                       pretty_print=True)
                    except etree.XMLSyntaxError:
                        continue  # skip non-XML files

        # 3. Delete all files under Resources/Images
        resources_images_dirs = [os.path.join(tmpdir, 'Resources', 'Images'), os.path.join(tmpdir, "Documents", "1", "Resources", "Images"),
                                 os.path.join(tmpdir, "Resources", "Fonts"), os.path.join(tmpdir, "Documents", "1", "Resources", "Fonts")]
        for resources_images_dir in resources_images_dirs:
            if os.path.isdir(resources_images_dir):
                shutil.rmtree(resources_images_dir)

        

        # 4. Repack the cleaned directory into a new XPS
        with zipfile.ZipFile(output_xps_path, 'w', zipfile.ZIP_DEFLATED, 5) as zout:
            for foldername, _, filenames in os.walk(tmpdir):
                for filename in filenames:
                    file_path = os.path.join(foldername, filename)
                    arcname = os.path.relpath(file_path, tmpdir)
                    zout.write(file_path, arcname)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Prune XPS samples to remove unused files.")
    parser.add_argument(
        "dir",
        help="Path to the input directory containing XPS samples to prune."
    )

    os.makedirs("out/", exist_ok=True)


    args = parser.parse_args()
    input_dir = args.dir
    print(f"Pruning XPS samples in directory: {input_dir}")
    
    for root, dirs, files in os.walk(input_dir):
        print(f"Working on folder: {root}")
        simple_copy = False
        if root.split(os.sep)[1] == "ConformanceViolations":
            simple_copy = True
        for file in files:
            if file.endswith(".xps"):
                xps_file = os.path.join(root, file)
                if not simple_copy:
                    remove_images_from_xps(xps_file, "out/")
                else:
                    shutil.copy2(xps_file, "out/")
                   
    for root, dirs, files in os.walk("out/"):
        for file in files:
            if os.path.getsize(os.path.join(root, file)) > 800000:
                print(f"Deleting {file} because it is too large")
                os.remove(os.path.join(root, file))

                

