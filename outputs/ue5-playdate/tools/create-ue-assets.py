"""Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>."""
import json
import pathlib
import unreal as ue

root = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'Unreal/Content/Device/Data/device.json').read_text())
source = root / 'Unreal/ContentSource/Device'
library = ue.MaterialEditingLibrary
asset_tools = ue.AssetToolsHelpers.get_asset_tools()
textures = {}

def texture(filename, role):
    if filename in textures:
        return textures[filename]
    task = ue.AssetImportTask()
    task.filename = str(source / filename)
    task.destination_path = '/Game/Device/Textures'
    task.automated = True
    task.replace_existing = True
    task.save = True
    asset_tools.import_asset_tasks([task])
    t = task.get_objects()[0]
    t.set_editor_property('srgb', False)
    if role == 'environment':
        t.set_editor_property('compression_settings', ue.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
        t.set_editor_property('mip_gen_settings',ue.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        t.set_editor_property('filter',ue.TextureFilter.TF_BILINEAR)
        t.set_editor_property('never_stream',True)
    elif role == 'map':
        # These source images are small. Preserve their lettering instead of
        # adding BC1 block artifacts on top of the original JPEG compression.
        t.set_editor_property('compression_settings',ue.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    elif role == 'normalMap':
        t.set_editor_property('compression_settings', ue.TextureCompressionSettings.TC_NORMALMAP)
        t.set_editor_property('flip_green_channel', True)
    elif role in ('packedPBRMap', 'metalGlossMap', 'aoMap'):
        t.set_editor_property('srgb', False)
        t.set_editor_property('compression_settings', ue.TextureCompressionSettings.TC_MASKS)
    ue.EditorAssetLibrary.save_loaded_asset(t)
    textures[filename] = t
    return t

def expr(m, kind, x=0, y=0):
    return library.create_material_expression(m, kind, x, y)

def constant(m, value):
    e = expr(m, ue.MaterialExpressionConstant)
    e.set_editor_property('r', value)
    return e

def vector(m, color):
    e = expr(m, ue.MaterialExpressionConstant3Vector)
    e.set_editor_property('constant', color)
    return e

def connect(a, output, b, input_name):
    assert library.connect_material_expressions(a, output, b, input_name), (a, output, b, input_name)

def prop(e, output, property):
    assert library.connect_material_property(e, output, property), (e, output, property)

def material(name):
    path = '/Game/Device/Materials/M_' + name
    m = ue.load_asset(path)
    if not m:
        m = asset_tools.create_asset('M_'+name, '/Game/Device/Materials', ue.Material, ue.MaterialFactoryNew())
    library.delete_all_material_expressions(m)
    return m

# The original viewer uses its own IBL shader, including a prefiltered LogLuv
# panorama and nine SH terms. Keeping it in a UE material also works on mobile
# without Lumen, runtime reflection captures, or a desktop-only light rig.
shader = (root/'tools/device-ibl.hlsl').read_text()
raw_sh = manifest['environmentSH']
import math
factors = [1/(2*math.sqrt(math.pi)), -.5*math.sqrt(3/math.pi),
           .5*math.sqrt(3/math.pi), -.5*math.sqrt(3/math.pi),
           .5*math.sqrt(15/math.pi), -.5*math.sqrt(15/math.pi),
           .25*math.sqrt(5/math.pi), -.5*math.sqrt(15/math.pi),
           .25*math.sqrt(15/math.pi)]
terms = [f"float3({','.join(format(raw_sh[i*3+c]*factors[i], '.9g') for c in range(3))})"
         for i in range(9)]
shader = shader.replace('SH_COEFFICIENTS', 'const float3 sh[9]={' + ','.join(terms) + '};')

def sample(m, filename, role):
    e=expr(m,ue.MaterialExpressionTextureSample)
    e.set_editor_property('texture',texture(filename,role))
    sampler=ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    if role=='normalMap':sampler=ue.MaterialSamplerType.SAMPLERTYPE_NORMAL
    elif role in ('packedPBRMap','metalGlossMap','aoMap'):sampler=ue.MaterialSamplerType.SAMPLERTYPE_MASKS
    e.set_editor_property('sampler_type',sampler)
    return e

for definition in manifest['materials']:
    m = material(definition['name'])
    m.set_editor_property('shading_model',ue.MaterialShadingModel.MSM_UNLIT)
    # The panorama atlas and LogLuv decode need stable texture coordinates and
    # high precision on mobile, especially for the glossy metal components.
    m.set_editor_property('float_precision_mode',ue.MaterialFloatPrecisionMode.MFPM_FULL)
    color = definition['color']
    gamma_tint = ue.LinearColor(((color>>16)&255)/255, ((color>>8)&255)/255, (color&255)/255, 1)
    base = vector(m,gamma_tint)
    if definition['name'] == 'asheville_screen':
        screen = expr(m, ue.MaterialExpressionTextureSampleParameter2D)
        screen.set_editor_property('parameter_name', 'GameScreen')
        screen.set_editor_property('texture', texture(definition['map'], 'map'))
        screen.set_editor_property('sampler_type',ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        ink=vector(m,ue.LinearColor(.15,.16,.15,1))
        paper=vector(m,ue.LinearColor(.72,.75,.70,1))
        base=expr(m,ue.MaterialExpressionLinearInterpolate)
        connect(ink,'',base,'A');connect(paper,'',base,'B');connect(screen,'R',base,'Alpha')
    elif 'map' in definition:
        t=sample(m,definition['map'],'map')
        mult=expr(m,ue.MaterialExpressionMultiply)
        connect(t,'RGB',mult,'A');connect(base,'',mult,'B');base=mult
    ng=expr(m,ue.MaterialExpressionPixelNormalWS)
    normal=ng
    if 'normalMap' in definition:
        n=sample(m,definition['normalMap'],'normalMap')
        normal=expr(m,ue.MaterialExpressionTransform)
        # Default transform is tangent -> world; the UV basis is UE's.
        connect(n,'RGB',normal,'')
    packed_name=definition.get('packedPBRMap',definition.get('metalGlossMap'))
    if packed_name:
        packed=sample(m,packed_name,'packedPBRMap')
        if 'packedPBRMap' not in definition:
            # Metal/gloss maps have no AO channel. Their green value is not
            # occlusion; the original shader uses AO=1 for the screen border.
            rg=expr(m,ue.MaterialExpressionAppendVector)
            connect(packed,'R',rg,'A');connect(constant(m,1),'',rg,'B')
            rgb=expr(m,ue.MaterialExpressionAppendVector)
            connect(rg,'',rgb,'A');connect(packed,'B',rgb,'B');packed=rgb
    else:
        packed=vector(m,ue.LinearColor(1,1,1,1))
        if 'aoMap' in definition:
            ao=sample(m,definition['aoMap'],'aoMap')
            rg=expr(m,ue.MaterialExpressionAppendVector)
            connect(constant(m,1),'',rg,'A');connect(ao,'R',rg,'B')
            packed=expr(m,ue.MaterialExpressionAppendVector)
            connect(rg,'',packed,'A');connect(constant(m,1),'',packed,'B')
    env=expr(m,ue.MaterialExpressionTextureObject)
    env.set_editor_property('texture',texture('EnvironmentLogLuv.png','environment'))
    env.set_editor_property('sampler_type',ue.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    view=expr(m,ue.MaterialExpressionCameraVectorWS)
    custom=expr(m,ue.MaterialExpressionCustom)
    custom.set_editor_property('description','Original Playdate environment lighting')
    custom.set_editor_property('code',shader)
    custom.set_editor_property('output_type',ue.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs={'Albedo':base,'Packed':packed,'N':normal,'Ng':ng,'V':view,'Environment':env,
            'MetalFactor':constant(m,definition['metallic']),
            'GlossFactor':constant(m,1-definition['roughness']),
            'Exposure':constant(m,.35 if definition['name'] in ('asheville_screen','asheville_screen_border') else 1)}
    custom_inputs=[]
    for name in inputs:
        item=ue.CustomInput()
        item.set_editor_property('input_name',name)
        custom_inputs.append(item)
    custom.set_editor_property('inputs',custom_inputs)
    for name,e in inputs.items():connect(e,'RGB' if e==packed and 'packedPBRMap' in definition else '',custom,name)
    prop(custom,'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
    library.recompile_material(m)
    ue.EditorAssetLibrary.save_loaded_asset(m)

m=material('GroundShadow')
m.set_editor_property('shading_model',ue.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode',ue.BlendMode.BLEND_TRANSLUCENT)
prop(vector(m,ue.LinearColor(0,0,0,1)),'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
t=sample(m,'ground_shadow.png','shadow')
opacity=expr(m,ue.MaterialExpressionMultiply)
connect(t,'A',opacity,'A');connect(constant(m,.65),'',opacity,'B')
prop(opacity,'',ue.MaterialProperty.MP_OPACITY)
library.recompile_material(m);ue.EditorAssetLibrary.save_loaded_asset(m)

m=material('Backdrop')
m.set_editor_property('shading_model',ue.MaterialShadingModel.MSM_UNLIT)
prop(vector(m,ue.LinearColor(.807,.807,.807,1)),'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
library.recompile_material(m); ue.EditorAssetLibrary.save_loaded_asset(m)
ue.EditorAssetLibrary.make_directory('/Game/Maps')
world=ue.get_editor_subsystem(ue.LevelEditorSubsystem)
if ue.EditorAssetLibrary.does_asset_exist('/Game/Maps/Device'):
    assert world.load_level('/Game/Maps/Device')
else:
    assert world.new_level('/Game/Maps/Device')
assert world.save_current_level()
ue.log('Crank device assets and default map saved')
