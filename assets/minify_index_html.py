Import("env")

env.Execute("$PYTHONEXE -m pip install minify_html")


def minify_html(src_path: str, target_path: str, variable_name: str = None):
    import os
    import minify_html

    if not variable_name:
        variable_name = (
            os.path.splitext(os.path.basename(src_path))[0].upper() + "_TEMPLATE"
        )

    with open(src_path, "r") as fd:
        html = fd.read()
    minified = minify_html.minify(html, minify_css=True)
    minified = minified.replace('"', '\\"')

    with open(target_path, "w") as fd:
        fd.write(f'constexpr const char* const {variable_name} = "{minified}";')


def pre_minify_index_html(source, target, env):
    import os

    root_folder = "assets"
    root_folder_prefix_len = len(root_folder) + 1

    for folder, _, files in os.walk(root_folder):
        for file in files:
            if not file.endswith(".html"):
                continue

            target_folder = os.path.join("include", folder[root_folder_prefix_len:])
            os.makedirs(target_folder, exist_ok=True)

            minify_html(
                os.path.join(folder, file),
                os.path.join(target_folder, file + ".h"),
            )


pre_minify_index_html(None, None, None)


# env.AddPreAction("compile", pre_minify_index_html)
