import json
import os
import re
import traceback

import unreal


# ============================================================
# USER CONFIG
# ============================================================

# Need to modify
# Export to Saved/GLTFExports/*
# SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(unreal.Paths.project_saved_dir(), "GLTFExports")
OUTPUT_FILE = os.path.join(DATA_DIR, "full_scene_auto_gltf_materials.glb")
REPORT_FILE = os.path.join(DATA_DIR, "full_scene_auto_gltf_materials_report.json")

# Generated Unreal material assets will be saved here.
GENERATED_MATERIAL_FOLDER = "/Game/Generated/GLTF_AutoMaterials"

# If False, one generated material is created per unique original material.
# If True, a unique generated material is created per actor/component/material slot.
# Usually False is better and cleaner.
CREATE_UNIQUE_MATERIAL_PER_SLOT = False

# If True, existing generated materials are reused.
# If False, existing generated materials are overwritten/recreated where possible.
REUSE_EXISTING_GENERATED_MATERIALS = True

# Exclude these actors from export.
EXCLUDE_ACTOR_NAME_CONTAINS = [
    # Cameras
    "camera",
    "cinecamera",
    "cam_",
    "viewport",

    # Lights
    "light",
    "lighting",
    "lighting exterior",
    "directional",
    "skylight",
    "spotlight",
    "pointlight",
    "rectlight",

    # Sky / environment domes that often export as a huge white bubble
    "sky",
    "sky_sphere",
    "skysphere",
    "bp_sky",
    "bp_sky_sphere",
    "skybox",
    "skydome",
    "sky_dome",
    "atmosphere",
    "skyatmosphere",
    "hdribackdrop",
    "hdri",
    "backdrop",
    "sphere_reflection",
    "reflectioncapture",
    "reflection_capture",
    "volumetriccloud",
    "exponentialheightfog",
    "fog",

    # Your known unwanted models
    "kiva",
    "forklift",
]

EXCLUDE_ACTOR_CLASS_CONTAINS = [
    # Cameras
    "cameraactor",
    "cinecameraactor",

    # Lights
    "pointlight",
    "spotlight",
    "directionallight",
    "rectlight",
    "skylight",
    "light",

    # Sky / environment / fog / reflection actors
    "skyatmosphere",
    "skylight",
    "exponentialheightfog",
    "volumetriccloud",
    "reflectioncapture",
    "spherecapture",
    "boxreflectioncapture",
    "hdribackdrop",
]

SKIP_HIDDEN_EDITOR_ACTORS = True
EXPORT_ONLY_MESH_ACTORS = True

# Texture name matching.
BASE_COLOR_KEYWORDS = [
    "basecolor",
    "base_color",
    "albedo",
    "diffuse",
    "color",
    "colour",
    "_bc",
    "_d",
]
NORMAL_KEYWORDS = ["normal", "nrm", "_n"]
ORM_KEYWORDS = [
    "orme",
    "orm",
    "occlusionroughnessmetallic",
    "occlusion_roughness_metallic",
    "roughnessmetallic",
    "arm",
    "rao",
]
ROUGHNESS_KEYWORDS = ["roughness", "_rgh", "_rough"]
METALLIC_KEYWORDS = ["metallic", "metalness", "_metal"]
AO_KEYWORDS = ["ao", "ambientocclusion", "occlusion"]
OPACITY_KEYWORDS = ["opacity", "alpha", "mask", "color_mask", "colour_mask"]


# ============================================================
# LOGGING
# ============================================================

def log(msg):
    unreal.log(f"[Auto GLTF Material Export] {msg}")
    print(f"[Auto GLTF Material Export] {msg}")


def warn(msg):
    unreal.log_warning(f"[Auto GLTF Material Export] {msg}")
    print(f"[Auto GLTF Material Export WARNING] {msg}")


# ============================================================
# SAFE HELPERS
# ============================================================

def safe_get(obj, prop, default=None):
    try:
        return obj.get_editor_property(prop)
    except Exception:
        try:
            return getattr(obj, prop)
        except Exception:
            return default


def safe_set(obj, prop, value):
    try:
        obj.set_editor_property(prop, value)
        return True
    except Exception:
        try:
            setattr(obj, prop, value)
            return True
        except Exception:
            return False


def sanitize_asset_name(name):
    name = re.sub(r"[^A-Za-z0-9_]", "", name)
    name = re.sub(r"_+", "_", name)
    return name.strip("_")


def ensure_folder(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def asset_exists(path):
    return unreal.EditorAssetLibrary.does_asset_exist(path)


def save_asset(asset):
    unreal.EditorAssetLibrary.save_loaded_asset(asset)


# ============================================================
# ACTOR / COMPONENT HELPERS
# ============================================================

def should_exclude_actor(actor):
    actor_name = actor.get_name().lower()
    actor_label = actor.get_actor_label().lower()
    class_name = actor.get_class().get_name().lower()

    combined = f"{actor_name} {actor_label}"

    for text in EXCLUDE_ACTOR_NAME_CONTAINS:
        if text.lower() in combined:
            return True

    for text in EXCLUDE_ACTOR_CLASS_CONTAINS:
        if text.lower() in class_name:
            return True

    if SKIP_HIDDEN_EDITOR_ACTORS and actor.is_hidden_ed():
        return True

    return False


def get_mesh_components(actor):
    components = []

    try:
        components.extend(actor.get_components_by_class(unreal.MeshComponent))
    except Exception:
        pass

    if not components:
        fallback_classes = [
            "StaticMeshComponent",
            "SkeletalMeshComponent",
            "InstancedStaticMeshComponent",
            "HierarchicalInstancedStaticMeshComponent",
        ]

        for cls_name in fallback_classes:
            cls = getattr(unreal, cls_name, None)
            if cls is not None:
                try:
                    components.extend(actor.get_components_by_class(cls))
                except Exception:
                    pass

    unique = []
    seen = set()

    for comp in components:
        if comp is None:
            continue

        key = comp.get_path_name()
        if key in seen:
            continue

        seen.add(key)
        unique.append(comp)

    return unique


def component_has_renderable_mesh(comp):
    static_mesh = safe_get(comp, "static_mesh", None)
    skeletal_mesh = safe_get(comp, "skeletal_mesh", None)

    if static_mesh is not None or skeletal_mesh is not None:
        return True

    try:
        return comp.get_num_materials() > 0
    except Exception:
        return False


def get_num_material_slots(comp):
    try:
        return comp.get_num_materials()
    except Exception:
        count = 0

        while True:
            try:
                mat = comp.get_material(count)
                if mat is None:
                    break

                count += 1
            except Exception:
                break

        return count


# ============================================================
# TEXTURE DISCOVERY
# ============================================================

def asset_data_to_asset(asset_data):
    try:
        return asset_data.get_asset()
    except Exception:
        try:
            return unreal.load_asset(str(asset_data.object_path))
        except Exception:
            return None


def get_asset_package_name(asset):
    try:
        asset_data = unreal.EditorAssetLibrary.find_asset_data(asset.get_path_name())
        return str(asset_data.package_name)
    except Exception:
        # Fallback: remove ".AssetName" from "/Game/Path/Asset.Asset".
        path = asset.get_path_name()
        if "." in path:
            return path.split(".")[0]

        return path


def get_texture_dependencies(material):
    """
    Uses Asset Registry dependencies to find texture assets referenced by a material
    or material instance.

    This is useful because Material Instances often reference textures without
    exposing a simple graph.
    """
    textures = []

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    package_name = get_asset_package_name(material)

    dep_options = unreal.AssetRegistryDependencyOptions(
        include_soft_package_references=True,
        include_hard_package_references=True,
        include_searchable_names=False,
        include_soft_management_references=False,
        include_hard_management_references=False,
    )

    try:
        dependencies = registry.get_dependencies(package_name, dep_options)
    except Exception:
        dependencies = []

    seen = set()

    for dep_package in dependencies:
        try:
            asset_data_list = registry.get_assets_by_package_name(dep_package)
        except Exception:
            asset_data_list = []

        for asset_data in asset_data_list:
            asset = asset_data_to_asset(asset_data)
            if asset is None:
                continue

            class_name = asset.get_class().get_name()

            if "Texture" not in class_name:
                continue

            path = asset.get_path_name()

            if path in seen:
                continue

            seen.add(path)
            textures.append(asset)

    return textures


def name_matches(name, keywords):
    lower = name.lower()

    for keyword in keywords:
        if keyword.lower() in lower:
            return True

    return False


def classify_textures(textures):
    result = {
        "base_color": None,
        "normal": None,
        "orm": None,
        "roughness": None,
        "metallic": None,
        "ao": None,
        "opacity": None,
        "all": [],
    }

    result["all"] = [t.get_path_name() for t in textures]

    for tex in textures:
        name = tex.get_name().lower()

        # Check specific packed/technical maps before broad "color".
        if result["normal"] is None and name_matches(name, NORMAL_KEYWORDS):
            result["normal"] = tex
            continue

        if result["orm"] is None and name_matches(name, ORM_KEYWORDS):
            result["orm"] = tex
            continue

        if result["roughness"] is None and name_matches(name, ROUGHNESS_KEYWORDS):
            result["roughness"] = tex
            continue

        if result["metallic"] is None and name_matches(name, METALLIC_KEYWORDS):
            result["metallic"] = tex
            continue

        if result["ao"] is None and name_matches(name, AO_KEYWORDS):
            result["ao"] = tex
            continue

        if result["opacity"] is None and name_matches(name, OPACITY_KEYWORDS):
            result["opacity"] = tex
            continue

        if result["base_color"] is None and name_matches(name, BASE_COLOR_KEYWORDS):
            # Avoid accidentally picking mask/normal/ORM as base color.
            if not name_matches(name, NORMAL_KEYWORDS + ORM_KEYWORDS + OPACITY_KEYWORDS):
                result["base_color"] = tex
                continue

    # Fallback: if only one texture exists and nothing matched, treat it as base color.
    if result["base_color"] is None and len(textures) == 1:
        result["base_color"] = textures[0]

    return result


# ============================================================
# MATERIAL GRAPH CREATION
# ============================================================

def create_texture_sample(material, texture, x, y, sampler_type=None):
    expr = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionTextureSample,
        x,
        y,
    )

    safe_set(expr, "texture", texture)

    if sampler_type is not None:
        safe_set(expr, "sampler_type", sampler_type)

    return expr


def create_constant(material, value, x, y):
    expr = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionConstant,
        x,
        y,
    )

    safe_set(expr, "r", float(value))
    return expr


def create_constant3(material, color, x, y):
    expr = unreal.MaterialEditingLibrary.create_material_expression(
        material,
        unreal.MaterialExpressionConstant3Vector,
        x,
        y,
    )

    safe_set(expr, "constant", color)
    return expr


def connect_material(expr, output_name, material_property):
    try:
        unreal.MaterialEditingLibrary.connect_material_property(
            expr,
            output_name,
            material_property,
        )
        return True
    except Exception as e:
        warn(f"Failed to connect material property {material_property}: {e}")
        return False


def copy_basic_material_properties(original_mat, new_mat):
    """
    Copies a few simple rendering properties.

    These are safe-ish. The material graph itself is not copied.
    """
    for prop in [
        "blend_mode",
        "two_sided",
        "shading_model",
        "opacity_mask_clip_value",
    ]:
        value = safe_get(original_mat, prop, None)
        if value is not None:
            safe_set(new_mat, prop, value)


def create_gltf_safe_material(original_mat, generated_asset_name, texture_info):
    ensure_folder(GENERATED_MATERIAL_FOLDER)

    material_path = f"{GENERATED_MATERIAL_FOLDER}/{generated_asset_name}.{generated_asset_name}"

    if REUSE_EXISTING_GENERATED_MATERIALS and asset_exists(material_path):
        existing = unreal.load_asset(material_path)
        if existing is not None:
            log(f"Reusing generated material: {material_path}")
            return existing, material_path, False

    # Delete existing if not reusing.
    if asset_exists(material_path):
        unreal.EditorAssetLibrary.delete_asset(material_path)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    new_mat = asset_tools.create_asset(
        generated_asset_name,
        GENERATED_MATERIAL_FOLDER,
        unreal.Material,
        unreal.MaterialFactoryNew(),
    )

    if new_mat is None:
        raise RuntimeError(f"Failed to create material: {material_path}")

    copy_basic_material_properties(original_mat, new_mat)

    base_color_tex = texture_info.get("base_color")
    normal_tex = texture_info.get("normal")
    orm_tex = texture_info.get("orm")
    roughness_tex = texture_info.get("roughness")
    metallic_tex = texture_info.get("metallic")
    ao_tex = texture_info.get("ao")
    opacity_tex = texture_info.get("opacity")

    # ------------------------------------------------------------
    # Base Color
    # ------------------------------------------------------------
    if base_color_tex is not None:
        base_expr = create_texture_sample(new_mat, base_color_tex, -700, -250)
        connect_material(base_expr, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    else:
        # Fallback colour if no base colour texture can be be found.
        color_expr = create_constant3(
            new_mat,
            unreal.LinearColor(0.5, 0.5, 0.5, 1.0),
            -700,
            -250,
        )
        connect_material(color_expr, "", unreal.MaterialProperty.MP_BASE_COLOR)

    # ------------------------------------------------------------
    # Normal
    # ------------------------------------------------------------
    if normal_tex is not None:
        sampler_type = None
        if hasattr(unreal, "MaterialSamplerType"):
            sampler_type = getattr(unreal.MaterialSamplerType, "SAMPLERTYPE_NORMAL", None)

        normal_expr = create_texture_sample(new_mat, normal_tex, -700, 50, sampler_type)
        connect_material(normal_expr, "RGB", unreal.MaterialProperty.MP_NORMAL)

    # ------------------------------------------------------------
    # ORM / ORME packed map
    # Common assumption:
    # R = Ambient Occlusion
    # G = Roughness
    # B = Metallic
    # ------------------------------------------------------------
    if orm_tex is not None:
        orm_expr = create_texture_sample(new_mat, orm_tex, -700, 350)

        connect_material(orm_expr, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
        connect_material(orm_expr, "G", unreal.MaterialProperty.MP_ROUGHNESS)
        connect_material(orm_expr, "B", unreal.MaterialProperty.MP_METALLIC)
    else:
        # Roughness fallback
        if roughness_tex is not None:
            rough_expr = create_texture_sample(new_mat, roughness_tex, -700, 350)
            connect_material(rough_expr, "R", unreal.MaterialProperty.MP_ROUGHNESS)
        else:
            rough_const = create_constant(new_mat, 0.5, -700, 350)
            connect_material(rough_const, "", unreal.MaterialProperty.MP_ROUGHNESS)

        # Metallic fallback
        if metallic_tex is not None:
            metal_expr = create_texture_sample(new_mat, metallic_tex, -700, 500)
            connect_material(metal_expr, "R", unreal.MaterialProperty.MP_METALLIC)
        else:
            metal_const = create_constant(new_mat, 0.0, -700, 500)
            connect_material(metal_const, "", unreal.MaterialProperty.MP_METALLIC)

        # AO fallback
        if ao_tex is not None:
            ao_expr = create_texture_sample(new_mat, ao_tex, -700, 650)
            connect_material(ao_expr, "R", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)

    # ------------------------------------------------------------
    # Opacity Mask
    # Only connect if the original material is masked/translucent or an
    # opacity texture exists.
    # ------------------------------------------------------------
    original_blend = str(safe_get(original_mat, "blend_mode", "")).lower()

    if opacity_tex is not None and (
        "masked" in original_blend or "translucent" in original_blend
    ):
        opacity_expr = create_texture_sample(new_mat, opacity_tex, -700, 800)

        # Usually masks use R or A. Try R first because many mask textures are RGB masks.
        connect_material(opacity_expr, "R", unreal.MaterialProperty.MP_OPACITY_MASK)

    elif base_color_tex is not None and (
        "masked" in original_blend or "translucent" in original_blend
    ):
        # If the base color has alpha, this can preserve cutouts.
        alpha_expr = create_texture_sample(new_mat, base_color_tex, -700, 800)
        connect_material(alpha_expr, "A", unreal.MaterialProperty.MP_OPACITY_MASK)

    # Layout and compile.
    try:
        unreal.MaterialEditingLibrary.layout_material_expressions(new_mat)
    except Exception:
        pass

    try:
        unreal.MaterialEditingLibrary.recompile_material(new_mat)
    except Exception as e:
        warn(f"Could not recompile material {generated_asset_name}: {e}")

    save_asset(new_mat)

    log(f"Created generated material: {material_path}")
    return new_mat, material_path, True


def get_generated_material_name(original_mat, actor=None, component=None, slot_index=None):
    original_name = sanitize_asset_name(original_mat.get_name())

    if CREATE_UNIQUE_MATERIAL_PER_SLOT and actor is not None and component is not None:
        actor_name = sanitize_asset_name(actor.get_actor_label())
        comp_name = sanitize_asset_name(component.get_name())
        return f"M_GLTF_Auto_{actor_name}_{comp_name}_{slot_index}_{original_name}"

    return f"M_GLTF_Auto_{original_name}"


# ============================================================
# GLTF EXPORT OPTIONS
# ============================================================

def make_gltf_options():
    options = unreal.GLTFExportOptions()

    # Generated materials are intentionally simple, so do not bake.
    safe_set(options, "bake_material_inputs", unreal.GLTFMaterialBakeMode.DISABLED)

    # Important: make sure textures are exported.
    if hasattr(unreal, "GLTFTextureImageFormat"):
        safe_set(options, "texture_image_format", unreal.GLTFTextureImageFormat.PNG)

    safe_set(options, "export_vertex_colors", False)
    safe_set(options, "adjust_normalmaps", True)

    # We are directly assigning generated materials, not using proxy lookup.
    safe_set(options, "export_proxy_materials", False)

    safe_set(options, "export_cameras", False)
    safe_set(options, "export_lights", 0)

    return options


# ============================================================
# MAIN EXPORT LOGIC
# ============================================================

def export_scene_auto_create_gltf_materials():
    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    os.makedirs(os.path.dirname(REPORT_FILE), exist_ok=True)

    ensure_folder(GENERATED_MATERIAL_FOLDER)

    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors = actor_subsystem.get_all_level_actors()

    material_cache = {}
    material_report = {}

    restore_records = []
    actors_to_export = set()

    created_material_count = 0
    reused_material_count = 0
    replaced_slot_count = 0
    skipped_actor_count = 0
    missing_texture_materials = {}

    log(f"Scanning {len(all_actors)} actors...")

    for actor in all_actors:
        if should_exclude_actor(actor):
            skipped_actor_count += 1
            continue

        mesh_components = get_mesh_components(actor)

        if EXPORT_ONLY_MESH_ACTORS and not mesh_components:
            continue

        actor_has_mesh = False

        for comp in mesh_components:
            if not component_has_renderable_mesh(comp):
                continue

            actor_has_mesh = True
            slot_count = get_num_material_slots(comp)

            for slot_index in range(slot_count):
                original_mat = comp.get_material(slot_index)

                if original_mat is None:
                    continue

                original_path = original_mat.get_path_name()
                original_name = original_mat.get_name()

                cache_key = original_path

                if CREATE_UNIQUE_MATERIAL_PER_SLOT:
                    cache_key = (
                        f"{actor.get_path_name()}::"
                        f"{comp.get_path_name()}::"
                        f"{slot_index}::"
                        f"{original_path}"
                    )

                if cache_key in material_cache:
                    generated_mat = material_cache[cache_key]["material"]
                    generated_path = material_cache[cache_key]["path"]
                    was_created = False
                else:
                    textures = get_texture_dependencies(original_mat)
                    texture_info = classify_textures(textures)

                    if texture_info["base_color"] is None:
                        missing_texture_materials[original_path] = {
                            "name": original_name,
                            "referenced_textures": texture_info["all"],
                            "reason": (
                                "No obvious base colour texture found. "
                                "Generated material will use grey fallback."
                            ),
                        }

                    generated_name = get_generated_material_name(
                        original_mat,
                        actor,
                        comp,
                        slot_index,
                    )

                    generated_mat, generated_path, was_created = create_gltf_safe_material(
                        original_mat,
                        generated_name,
                        texture_info,
                    )

                    material_cache[cache_key] = {
                        "material": generated_mat,
                        "path": generated_path,
                        "was_created": was_created,
                        "texture_info": {
                            "base_color": (
                                texture_info["base_color"].get_path_name()
                                if texture_info["base_color"]
                                else None
                            ),
                            "normal": (
                                texture_info["normal"].get_path_name()
                                if texture_info["normal"]
                                else None
                            ),
                            "orm": (
                                texture_info["orm"].get_path_name()
                                if texture_info["orm"]
                                else None
                            ),
                            "roughness": (
                                texture_info["roughness"].get_path_name()
                                if texture_info["roughness"]
                                else None
                            ),
                            "metallic": (
                                texture_info["metallic"].get_path_name()
                                if texture_info["metallic"]
                                else None
                            ),
                            "ao": (
                                texture_info["ao"].get_path_name()
                                if texture_info["ao"]
                                else None
                            ),
                            "opacity": (
                                texture_info["opacity"].get_path_name()
                                if texture_info["opacity"]
                                else None
                            ),
                            "all": texture_info["all"],
                        },
                    }

                    if was_created:
                        created_material_count += 1
                    else:
                        reused_material_count += 1

                restore_records.append(
                    {
                        "component": comp,
                        "slot_index": slot_index,
                        "original_material": original_mat,
                        "generated_material": generated_mat,
                        "actor_label": actor.get_actor_label(),
                    }
                )

                comp.set_material(slot_index, generated_mat)
                replaced_slot_count += 1

                if original_path not in material_report:
                    material_report[original_path] = {
                        "original_name": original_name,
                        "generated_material": generated_path,
                        "used_by": [],
                    }

                    if cache_key in material_cache:
                        material_report[original_path]["textures"] = material_cache[
                            cache_key
                        ]["texture_info"]

                material_report[original_path]["used_by"].append(
                    {
                        "actor": actor.get_actor_label(),
                        "component": comp.get_name(),
                        "slot": slot_index,
                    }
                )

        if actor_has_mesh:
            actors_to_export.add(actor)

    log(f"Actors skipped: {skipped_actor_count}")
    log(f"Actors to export: {len(actors_to_export)}")
    log(f"Generated materials created: {created_material_count}")
    log(f"Generated materials reused: {reused_material_count}")
    log(f"Material slots temporarily replaced: {replaced_slot_count}")
    log(f"Materials without obvious base colour texture: {len(missing_texture_materials)}")

    if not actors_to_export:
        raise RuntimeError("No mesh actors found to export.")

    world = unreal.EditorLevelLibrary.get_editor_world()
    options = make_gltf_options()

    export_result = None

    try:
        log(f"Exporting scene to: {OUTPUT_FILE}")

        export_result = unreal.GLTFExporter.export_to_gltf(
            world,
            OUTPUT_FILE,
            options,
            actors_to_export,
        )

        log(f"Export complete. Result: {export_result}")

    finally:
        log("Restoring original materials...")

        for record in restore_records:
            comp = record["component"]
            slot_index = record["slot_index"]
            original_mat = record["original_material"]

            if comp is not None:
                comp.set_material(slot_index, original_mat)

        log(f"Restored {len(restore_records)} material slots.")

        report = {
            "status": "complete",
            "output_file": OUTPUT_FILE,
            "export_result": str(export_result),
            "generated_material_folder": GENERATED_MATERIAL_FOLDER,
            "actors_exported": len(actors_to_export),
            "material_slots_replaced": replaced_slot_count,
            "generated_materials_created": created_material_count,
            "generated_materials_reused": reused_material_count,
            "materials_without_obvious_base_color": missing_texture_materials,
            "material_report": material_report,
        }

        with open(REPORT_FILE, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)

        log(f"Report written to: {REPORT_FILE}")

    log("Done.")


try:
    export_scene_auto_create_gltf_materials()
except Exception:
    error = traceback.format_exc()
    unreal.log_error(error)
    print(error)
    raise
