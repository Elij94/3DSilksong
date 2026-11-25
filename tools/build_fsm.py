#!/usr/bin/env python3
"""Compiles a room's PlayMaker state machines (romfs/fsm/<Scene>.json) into the engine's compact form,
romfs/rooms/<Scene>.ssf (format: docs/FORMATS.md), with the scene's hierarchy (work/rooms/<Scene>.tree.json) and its
objects (work/rooms/<Scene>.objs.json). Only the actions in OPS are kept, with their parameters in the order given
there; the others become no-ops. Also writes source/fsm_ops.h, the op numbers the engine shares.

  python tools/build_fsm.py romfs [Scene ...]      (no scene: every room with an FSM file)
"""
import json, os, struct, sys, collections

WORK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work')

# op -> parameter names (first found of a|b), in the order the engine reads them
OPS = [
    ('Wait', 'time finishEvent'),
    ('WaitRandom', 'timeMin timeMax finishEvent'),
    ('NextFrameEvent', 'sendEvent'),
    ('SendEvent', 'eventTarget sendEvent delay'),
    ('SendEventByName', 'eventTarget sendEvent delay'),
    ('SendRandomEvent', 'events weights'),
    ('SendRandomEventV3', 'events weights'),
    ('SendRandomEventV4', 'events weights'),
    ('BoolTest', 'boolVariable isTrue isFalse'),
    ('SetBoolValue', 'boolVariable boolValue'),
    ('BoolTestMulti', 'boolVariables boolStates trueEvent falseEvent storeResult'),
    ('BoolAllTrue', 'boolVariables sendEvent storeResult'),
    ('BoolNoneTrue', 'boolVariables sendEvent storeResult'),
    ('BoolFlip', 'boolVariable'),
    ('SetFloatValue', 'floatVariable floatValue'),
    ('FloatAdd', 'floatVariable add perSecond'),
    ('FloatSubtract', 'floatVariable subtract perSecond'),
    ('FloatMultiply', 'floatVariable multiplyBy'),
    ('FloatClamp', 'floatVariable minValue maxValue'),
    ('FloatAbs', 'floatVariable'),
    ('FloatOperator', 'float1 float2 operation storeResult'),
    ('FloatCompare', 'float1 float2 tolerance equal lessThan greaterThan'),
    ('FloatSignTest', 'floatValue isPositive isNegative'),
    ('RandomFloat', 'min max storeResult'),
    ('SetIntValue', 'intVariable intValue'),
    ('IntAdd', 'intVariable add'),
    ('IntOperator', 'integer1 integer2 operation storeResult'),
    ('IntCompare', 'integer1 integer2 equal lessThan greaterThan'),
    ('RandomInt', 'min max storeResult inclusiveMax'),
    ('ConvertIntToFloat', 'intVariable floatVariable'),
    ('ConvertBoolToFloat', 'boolVariable floatVariable falseValue trueValue'),
    ('SetStringValue', 'stringVariable stringValue'),
    ('StringCompare', 'stringVariable compareTo equalEvent notEqualEvent'),
    ('EnumCompare', 'enumVariable compareTo equalEvent notEqualEvent'),
    ('SetGameObject', 'variable gameObject'),
    ('GetOwner', 'storeGameObject'),
    ('FindChild', 'gameObject childName storeResult'),
    ('GetParent', 'gameObject storeResult'),
    ('GameObjectIsNull', 'gameObject isNull isNotNull'),
    ('GameObjectCompare', 'gameObjectVariable compareTo equalEvent notEqualEvent'),
    ('ActivateGameObject', 'gameObject activate'),
    ('ActivateAllChildren', 'gameObject activate'),
    ('SetPosition', 'gameObject vector x y space'),
    ('SetPosition2d', 'gameObject vector x y space'),
    ('SetPosition2D', 'GameObject Vector X Y Space'),
    ('GetPosition', 'gameObject vector x y space'),
    ('GetPosition2D', 'gameObject|GameObject vector|Vector x|X y|Y space|Space'),
    ('Translate', 'gameObject vector x y space perSecond'),
    ('SetScale', 'gameObject vector x y'),
    ('SetVelocity2d', 'gameObject vector x y'),
    ('SetVelocityByScale', 'gameObject speed ySpeed'),
    ('GetVelocity2d', 'gameObject vector x y'),
    ('DecelerateV2', 'gameObject deceleration'),
    ('DecelerateXY', 'gameObject decelerationX decelerationY'),
    ('AccelerateToX', 'gameObject accelerationFactor targetSpeed'),
    ('SetGravity2dScale', 'gameObject gravityScale'),
    ('GetDistance', 'gameObject target storeResult'),
    ('FaceObjectV2', 'objectA objectB spriteFacesRight'),
    ('CheckTargetDirection', 'gameObject target aboveEvent belowEvent rightEvent leftEvent'),
    ('AnimatePositionBy', 'gameObject shiftBy time finishEvent'),
    ('AnimatePositionTo', 'gameObject toValue localSpace time finishEvent'),
    ('SetMeshRenderer', 'gameObject active'),
    ('SetSpriteRenderer', 'gameObject active'),
    ('SetCollider', 'gameObject active'),
    ('SetDamageHero', 'Target Enabled'),
    ('Tk2dPlayAnimation', 'gameObject clipName'),
    ('Tk2dPlayAnimationWithEvents', 'gameObject clipName animationTriggerEvent animationCompleteEvent'),
    ('Tk2dWatchAnimationEvents', 'gameObject animationTriggerEvent animationCompleteEvent'),
    ('Trigger2dEvent', 'gameObject trigger collideTag sendEvent'),
    ('Trigger2dEventLayer', 'gameObject trigger collideLayer sendEvent'),
    ('Collision2dEvent', 'gameObject collision collideTag sendEvent'),
    ('PlayerDataBoolTest', 'boolName isTrue isFalse'),
    ('SetPlayerDataBool', 'boolName value'),
    ('GetPlayerDataBool', 'boolName storeValue'),
    ('SetPlayerDataInt', 'intName value'),
    ('GetPlayerDataInt', 'intName storeValue'),
    ('SetFsmBool', 'gameObject fsmName variableName setValue'),
    ('GetFsmBool', 'gameObject fsmName variableName storeValue'),
    ('SetFsmFloat', 'gameObject fsmName variableName setValue'),
    ('GetFsmFloat', 'gameObject fsmName variableName storeValue'),
    ('SetFsmInt', 'gameObject fsmName variableName setValue'),
    ('SetFsmString', 'gameObject fsmName variableName setValue'),
    ('SetBoolValueAtTime', 'BoolVariable BoolValue Time'),
    ('SetVector2XY', 'vector2Variable vector2Value x y'),
    ('GetVector2XY', 'vector2Variable storeX storeY'),
    ('SetVector3XYZ', 'vector3Variable vector3Value x y'),
    ('GetVector3XYZ', 'vector3Variable storeX storeY'),
    ('DamageHeroDirectly', 'damager damageAmount'),
    ('ReceivedDamage', 'Target sendEvent sendEventHeavy'),
    ('WaitForHeroInPosition', 'sendEvent'),
    ('RunDialogue', 'Sheet Key Target'),
    ('ActivateInteractible', 'Target Activate'),
    ('FinishFSM', ''),
    ('PlayAudioEvent', 'audioClip volume spawnPoint'),
    ('RunDialogueV4', 'Sheet Key'),
    ('EndDialogue', 'ReturnControl'),
    ('RunDialogueV3', 'Sheet Key'),
    ('RunDialogueV2', 'Sheet Key'),
    ('AudioPlayerOneShotSingle', 'audioClip volume spawnPoint'),
    ('AudioPlaySimple', 'oneShotClip volume gameObject'),
    ('AudioPlayRandom', 'audioClips weights gameObject'),
    ('AudioPlayerOneShot', 'audioClips weights spawnPoint volume'),
    # enemies
    ('FindGameObject', 'objectName withTag store'),
    ('GetHero', 'storeResult'),
    ('CheckAlertRangeByName', 'eventTarget alertRangeName storeResult sendEvent outOfRangeEvent'),
    ('CheckAlertRange', 'alertRange storeResult InRangeEvent OutOfRangeEvent'),
    ('FindAlertRange', 'target storeResult childName'),
    ('RayCast2d', 'fromGameObject fromPosition direction space distance hitEvent noHitEvent storeDidHit storeHitPoint storeDistance'),
    ('RayCast2dV2', 'fromGameObject fromPosition direction space distance hitEvent noHitEvent storeDidHit storeHitPoint storeDistance'),
    ('CheckCollisionSide', 'collidingObject topHit rightHit bottomHit leftHit topHitEvent rightHitEvent bottomHitEvent leftHitEvent'),
    ('CheckCollisionSideEnter', 'collidingObject topHit rightHit bottomHit leftHit topHitEvent rightHitEvent bottomHitEvent leftHitEvent'),
    ('CheckIsCharacterGrounded', 'Target GroundDistance StoreResult GroundedEvent NotGroundedEvent SkinHeight'),
    ('FaceDirection', 'gameObject spriteFacesRight'),
    ('FaceObject', 'objectA objectB spriteFacesRight'),
    ('FlipScale', 'gameObject flipHorizontally flipVertically'),
    ('RandomlyFlipScale', 'gameObject'),
    ('GetScale', 'gameObject xScale yScale'),
    ('SendEventByScale', 'gameObject eventTarget positiveEvent negativeEvent'),
    ('CheckFacingTarget', 'facingObject target spriteFacesRight facingEvent notFacingEvent facingBool notFacingBool'),
    ('GetXDistance', 'gameObject target storeResult'),
    ('GetYDistance', 'gameObject target storeResult allowNegatives'),
    ('GetAngleToTarget2D', 'gameObject target offsetX offsetY storeAngle'),
    ('SetVelocityAsAngle', 'gameObject angle speed'),
    ('DistanceFly', 'gameObject target distance speedMax acceleration minAboveHero'),
    ('IdleBuzz', 'gameObject waitMin waitMax speedMax accelerationMax roamingRange roamingRange'),
    ('IdleBuzzV3', 'gameObject waitMin waitMax speedMax accelerationMax roamingRangeX roamingRangeY'),
    ('ChaseObject', 'gameObject target speedMax acceleration offsetX offsetY'),
    ('ChaseObjectV2', 'gameObject target speedMax accelerationForce offsetX offsetY'),
    ('ChaseObjectGround', 'gameObject target xOffset speedMax acceleration'),
    ('AccelerateToY', 'gameObject accelerationFactor targetSpeed'),
    ('SetPositionToObject', 'gameObject targetObject xOffset yOffset'),
    ('SetPositionToObject2D', 'gameObject targetObject xOffset yOffset'),
    ('FloatTestToBool', 'float1 float2 tolerance equalBool lessThanBool greaterThanBool'),
    ('FloatInRange', 'floatVariable lowerValue upperValue boolVariable trueEvent falseEvent'),
    ('Tk2dPlayFrame', 'gameObject frame'),
    ('SetIsKinematic2d', 'gameObject isKinematic'),
    ('SetInvincible', 'target Invincible'),
    ('SetRecoilSpeed', 'target newRecoilSpeed'),
    ('DoCameraShake', 'VisibleRenderer'),
    ('SetDamageHeroAmount', 'target damageDealt'),
    ('StartWalker', 'target walkRight'),
    ('StartWalkerV2', 'Target'),
    ('StopWalker', 'target'),
    ('FireAtTarget', 'gameObject target speed'),
    ('GetPosition2d', 'gameObject vector_2d x y'),
    ('StartCrawler', 'Target'),
    ('StopCrawler', 'Target'),
    # Hornet taken over by a script (cutscenes, shrines, benches...), and what scripts do around it
    ('CallMethodProper', 'gameObject behaviour methodName p_num p_bool'),
    ('HeroControllerMethods', 'method p_bool'),
    ('SendMessage', 'gameObject fn p_num'),
    ('ScreenFader', 'start_a end_a duration'),
    ('ListenForCast', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForJump', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForAttack', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForDash', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForUp', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForDown', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForLeft', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForRight', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForDreamNail', 'wasPressed wasReleased isPressed isNotPressed'),
    ('ListenForQuickCast', 'wasPressed wasReleased isPressed isNotPressed'),
    ('SendEventToRegister', 'eventName'),
    ('iTweenMoveBy', 'gameObject vector time delay finishEvent'),
    ('iTweenMoveTo', 'gameObject transformPosition vectorPosition time delay finishEvent'),
    ('ActivateGameObjectDelay', 'gameObject activate delay'),
    ('RandomFloatEither', 'value1 value2 storeResult'),
    ('GetPlayerDataVariable', 'VariableName StoreValue'),
    ('SetPlayerDataVariable', 'VariableName SetValue'),
    ('CheckHeroPerformanceRegion', 'Target MinReactDelay MaxReactDelay None ActiveInner ActiveOuter IgnoreNeedolinRange'),
    ('CheckHeroPerformanceRegionV2', 'Target Radius MinReactDelay MaxReactDelay None ActiveInner ActiveOuter '
                                     'IgnoreNeedolinRange'),
]
OP_ID = {n: i for i, (n, _) in enumerate(OPS)}


def flatten(t, p):
    """the parameters some actions nest: a method's first argument, a message's name, a fade's alphas"""
    p = dict(p)
    if t in ('CallMethodProper', 'HeroControllerMethods'):
        args = p.get('parameters') or []
        first = args[0] if args and isinstance(args[0], dict) else {}
        if 'var' in first:
            p['p_num'] = p['p_bool'] = first
        else:
            p['p_num'] = float(first.get('f') or first.get('i') or 0)
            p['p_bool'] = bool(first.get('b'))
    elif t == 'SendMessage':
        fc = p.get('functionCall') or {}
        p['fn'] = fc.get('function') or ''
        v = fc.get('v')
        p['p_num'] = float(v) if isinstance(v, (int, float)) else float(fc.get('f') or 0)
    elif t == 'ScreenFader':
        p['start_a'] = float((p.get('startColour') or {}).get('a', 0) or 0)
        p['end_a'] = float((p.get('endColour') or {}).get('a', 0) or 0)
    return p

T_NONE, T_FLOAT, T_INT, T_BOOL, T_STR, T_VAR, T_NODE, T_EVENT, T_VEC, T_LIST, T_TARGET = range(11)
N_OWNER, N_HERO, N_NONE = -2, -3, -1


class Strings:
    def __init__(self):
        self.ix, self.list = {}, []

    def __call__(self, s):
        s = '' if s is None else str(s)
        if s not in self.ix:
            self.ix[s] = len(self.list)
            self.list.append(s)
        return self.ix[s]

    def blob(self):
        b = struct.pack('<H', len(self.list))
        for s in self.list:
            e = s.encode('utf-8')[:255]
            b += struct.pack('<B', len(e)) + e
        return b


def compile_room(romfs, scene, out_path):
    fsms = json.load(open(os.path.join(romfs, 'fsm', scene + '.json'), encoding='utf-8'))
    tree = json.load(open(os.path.join(WORK, 'rooms', scene + '.tree.json')))
    objs_path = os.path.join(WORK, 'rooms', scene + '.objs.json')
    objs = json.load(open(objs_path)) if os.path.exists(objs_path) else []
    S = Strings()
    S('')
    node_of = {g[0]: i for i, g in enumerate(tree)}
    obj_of = {o['gid']: i for i, o in enumerate(objs) if 'gid' in o}
    hero_names = {'Hero_Hornet', 'Hero', 'Knight'}

    def node(gid):
        if gid in node_of:
            return node_of[gid]
        return N_NONE

    stats = collections.Counter()

    def val(v, vars_ix, kind=None):
        """-> bytes of one tagged value"""
        if v is None or (isinstance(v, dict) and v.get('none')):
            return struct.pack('<B', T_NONE)
        if isinstance(v, bool):
            return struct.pack('<BB', T_BOOL, int(v))
        if isinstance(v, int) and kind != 'float':
            return struct.pack('<Bi', T_INT, v)
        if isinstance(v, (int, float)):
            return struct.pack('<Bf', T_FLOAT, float(v))
        if isinstance(v, str):
            return struct.pack('<BH', T_STR, S(v))
        if isinstance(v, list):
            b = struct.pack('<BB', T_LIST, min(len(v), 255))
            for x in v[:255]:
                b += val(x, vars_ix)
            return b
        if isinstance(v, dict):
            if 'var' in v:
                return struct.pack('<BH', T_VAR, vars_ix.get(v['var'], 0xFFFF))
            if v.get('owner'):
                return struct.pack('<Bh', T_NODE, N_OWNER)
            if 'go' in v:
                if isinstance(v['go'], dict):    # FsmOwnerDefault around a reference
                    return val(v['go'], vars_ix)
                if v.get('guid'):        # an asset (prefab, sound): not a scene object
                    return struct.pack('<BH', T_STR, S(v['guid']))
                return struct.pack('<Bh', T_NODE, node(v['go']))
            if 'event' in v:
                return struct.pack('<BH', T_EVENT, S(v['event']))
            if 'target' in v:          # FsmEventTarget
                g = v.get('go')
                nd = N_OWNER if (g is None or (isinstance(g, dict) and g.get('owner'))) else \
                    (node(g['go']) if isinstance(g, dict) and 'go' in g else N_NONE)
                return struct.pack('<BBhH', T_TARGET, int(v.get('target') or 0), nd, S(v.get('fsmName') or ''))
            if set(v) & {'x', 'y', 'z'} and set(v) <= {'x', 'y', 'z', 'w'}:
                return struct.pack('<B3f', T_VEC, float(v.get('x', 0) or 0), float(v.get('y', 0) or 0),
                                   float(v.get('z', 0) or 0))
            if 'type' in v and 'f' in v:   # FsmVar
                t = str(v['type'])
                if t == '2': return struct.pack('<BB', T_BOOL, int(v.get('b') or 0))
                if t == '1': return struct.pack('<Bi', T_INT, int(v.get('i') or 0))
                if t == '0': return struct.pack('<Bf', T_FLOAT, float(v.get('f') or 0))
                if t == '3': return struct.pack('<BH', T_STR, S(v.get('s')))
        return struct.pack('<B', T_NONE)

    b_fsm = bytearray()
    nf = 0
    for f in fsms:
        owner = node(f.get('gid'))
        if owner < 0:
            stats['fsm without owner'] += 1
            continue
        nf += 1
        var_names = list(f.get('variables', {}).keys())
        vars_ix = {n: i for i, n in enumerate(var_names)}
        states = f.get('states', [])
        st_ix = {s['name']: i for i, s in enumerate(states)}
        b = struct.pack('<hHH', owner, S(f.get('fsm')), st_ix.get(f.get('start'), 0))
        b += struct.pack('<H', len(var_names))
        for n in var_names:
            v = f['variables'][n]
            t = v.get('type')
            value = v.get('value')
            kind = 'float' if t == 'float' else None
            if t == 'gameobject' and n in ('Hero', 'hero', 'Hero Obj', 'Hornet'):
                tv = struct.pack('<Bh', T_NODE, N_HERO)
            elif t == 'gameobject' and isinstance(value, dict) and 'go' in value:
                gid = value['go']
                tv = struct.pack('<Bh', T_NODE, node(gid) if not value.get('guid') else N_NONE)
            elif t in ('vector2', 'vector3') and isinstance(value, dict):
                tv = val(value, vars_ix)
            elif t in ('float', 'int', 'bool', 'string', 'enum'):
                if value is None:
                    value = {'float': 0.0, 'int': 0, 'bool': False, 'string': '', 'enum': 0}[t]
                if t == 'bool':
                    value = bool(value)
                tv = val(value, vars_ix, kind)
            else:
                tv = struct.pack('<B', T_NONE)
            b += struct.pack('<H', S(n)) + tv
        glob = f.get('global', [])
        b += struct.pack('<H', len(glob))
        for g in glob:
            b += struct.pack('<HH', S(g.get('event')), st_ix.get(g.get('to'), 0xFFFF))
        b += struct.pack('<H', len(states))
        for s in states:
            b += struct.pack('<H', S(s['name']))
            tr = s.get('transitions', [])
            b += struct.pack('<H', len(tr))
            for t in tr:
                b += struct.pack('<HH', S(t.get('event')), st_ix.get(t.get('to'), 0xFFFF))
            acts = []
            for a in s.get('actions', []):
                if a.get('enabled') is False:
                    continue
                t = a.get('type', '').split('.')[-1]
                if t not in OP_ID:
                    stats['skipped ' + t] += 1
                    continue
                acts.append((t, a.get('params') or {}))
            b += struct.pack('<H', len(acts))
            for t, p in acts:
                p = flatten(t, p)
                keys = OPS[OP_ID[t]][1].split()
                every = bool(p.get('everyFrame') or p.get('EveryFrame'))
                b += struct.pack('<BBB', OP_ID[t], int(every), len(keys))
                for k in keys:
                    v = None
                    for alt in k.split('|'):
                        if alt in p:
                            v = p[alt]
                            break
                    kind = 'float' if k in ('time', 'timeMin', 'timeMax', 'delay', 'x', 'y', 'X', 'Y', 'add',
                                            'float1', 'float2', 'floatValue', 'tolerance', 'min', 'max',
                                            'multiplyBy', 'minValue', 'maxValue', 'speed', 'deceleration',
                                            'Time', 'falseValue', 'trueValue', 'gravityScale', 'distance', 'speedMax',
                                            'acceleration', 'accelerationForce', 'accelerationMax', 'accelerationFactor',
                                            'targetSpeed', 'offsetX', 'offsetY', 'xOffset', 'yOffset', 'waitMin', 'waitMax',
                                            'roamingRange', 'roamingRangeX', 'roamingRangeY', 'minAboveHero', 'angle',
                                            'lowerValue', 'upperValue', 'newRecoilSpeed', 'GroundDistance',
                                            'SkinHeight', 'float1', 'float2', 'p_num', 'start_a', 'end_a',
                                            'duration', 'value1', 'value2', 'MinReactDelay',
                                            'MaxReactDelay', 'Radius') and t not in (
                                                'RandomInt', 'IntCompare', 'IntAdd') else None
                    b += val(v, vars_ix, kind)
                stats['kept'] += 1
        b_fsm += b

    b_nodes = bytearray(struct.pack('<H', len(tree)))
    for g in tree:
        gid, name, father, x, y, active = g[:6]
        rect = g[6] if len(g) > 6 and g[6] else [0, 0, 0, 0]
        body = g[7] if len(g) > 7 and g[7] else [-1, 0]
        sx = g[8] if len(g) > 8 else 1.0
        parent = node_of.get(father, -1)
        # u16 name, s16 parent, s16 object, f32 x, y, u8 active, s8 body type (-1 none, 0 dynamic, 1 kinematic,
        # 2 static), 2 pad, f32 rect x, y, w, h, f32 gravity scale, f32 scale x
        b_nodes += struct.pack('<HhhffBb2x6f', S(name), parent, obj_of.get(gid, -1), x, y, active, int(body[0]),
                               *[float(v) for v in rect], float(body[1]), float(sx))
    dyn_path = os.path.join(WORK, 'rooms', scene + '.dyn.json')
    dyn = json.load(open(dyn_path)) if os.path.exists(dyn_path) else []
    b_dyn = bytearray(struct.pack('<H', len(dyn)))
    for gid, x0, y0, x1, y1 in dyn:
        b_dyn += struct.pack('<h4f', node_of.get(gid, -1), x0, y0, x1, y1)
    # what a hit and a kill do, per enemy (objects.py: its hit / death effects, corpse, recoil)
    from objects import safe as lib_safe
    ens = [(i, o['enemy']) for i, o in enumerate(objs) if o.get('enemy')]
    b_en = bytearray(struct.pack('<H', len(ens)))
    for i, e in ens:
        fl = (1 if e.get('low_arc') else 0) | (2 if e.get('faces_right') else 0) | (4 if e.get('no_corpse') else 0) | \
             (8 if e.get('freeze') else 0) | (16 if 'recoil' in e else 0)
        rv, rd = e.get('recoil', [15.0, 0.15])
        snd = e.get('death_sounds', [])[:4]
        b_en += struct.pack('<hHHHfBffB', i, S(e.get('hit', '')), S(e.get('death', '')),
                            S(lib_safe(e['corpse_library']) if e.get('corpse_library') else ''), float(e.get('fling', 0)),
                            fl, float(rv), float(rd), len(snd))
        for g in snd:
            b_en += struct.pack('<H', S(g))
        w = e.get('walker')
        b_en += struct.pack('<B', 1 if w else 0)
        if w:
            b_en += struct.pack('<7f2Bff', float(w[0]), float(w[1]), float(w[2]), float(w[3]), float(w[4]), float(w[5]),
                                float(w[6]), int(w[7]), int(w[8]), float(w[9]), float(w[10]))
        c = e.get('crawler')
        b_en += struct.pack('<B', 1 if c else 0)
        if c:
            b_en += struct.pack('<fBHB', float(c[0]), int(c[1]), S(c[2]), int(c[3]))
        w2 = e.get('walker2')
        b_en += struct.pack('<B', 1 if w2 else 0)
        if w2:
            b_en += struct.pack('<fHHHffB', float(w2[0]), S(w2[1]), S(w2[2]), S(w2[3]), float(w2[4]), float(w2[5]), int(w2[6]))
        g = (e.get('geo') or [0] * 6) + [0] * 6
        b_en += struct.pack('<5HB', *(min(65535, max(0, int(x))) for x in g[:5]), 1 if g[5] else 0)
    # what objects remember and what PlayerData switches off (PersistentBoolItem, DeactivateIfPlayerdata*)
    sts = [(i, o['state']) for i, o in enumerate(objs) if o.get('state')]
    b_st = bytearray(struct.pack('<H', len(sts)))
    for i, st in sts:
        pe = st.get('persist')
        po = st.get('pd_off')
        b_st += struct.pack('<hBBH', i, 1 if pe else 0, int(pe[1]) if pe else 0,
                            S(pe[0] or str(objs[i].get('name'))) if pe else 0)
        b_st += struct.pack('<BBH', 1 if po else 0, int(po[1]) if po else 0, S(po[0]) if po else 0)
        ac = st.get('activator')            # TestGameObjectActivator (version 7)
        b_st += struct.pack('<B', 1 if ac else 0)
        if ac:
            groups = ac[2][:8]
            b_st += struct.pack('<hhB', node(ac[0]) if ac[0] else N_NONE, node(ac[1]) if ac[1] else N_NONE, len(groups))
            for g in groups:
                g = g[:8]
                b_st += struct.pack('<B', len(g))
                for t in g:
                    b_st += struct.pack('<BHBBif', int(t[0]), S(t[1]), int(t[2]), int(t[3]), int(t[4]), float(t[5]))
    out = b'SSFM' + struct.pack('<H', 7) + S.blob() + bytes(b_nodes) + struct.pack('<H', nf) + bytes(b_fsm) + bytes(b_dyn) + \
        bytes(b_en) + bytes(b_st)
    open(out_path, 'wb').write(out)
    return stats, nf


def write_header(path):
    lines = ['// generated by tools/build_fsm.py: the FSM actions the engine runs (parameters in this order)',
             '#pragma once', '']
    for i, (n, keys) in enumerate(OPS):
        lines.append(f'#define OP_{n:30s} {i:3d}   // {keys}')
    lines.append(f'#define OP_COUNT {len(OPS)}')
    open(path, 'w').write('\n'.join(lines) + '\n')


def main():
    romfs = sys.argv[1]
    scenes = sys.argv[2:] or sorted(f[:-5] for f in os.listdir(os.path.join(romfs, 'fsm')) if f.endswith('.json'))
    write_header(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'source', 'fsm_ops.h'))
    total = collections.Counter()
    for sc in scenes:
        if not os.path.exists(os.path.join(WORK, 'rooms', sc + '.tree.json')):
            print(f'{sc}: no tree (run build_room.py first)')
            continue
        if not os.path.exists(os.path.join(romfs, 'fsm', sc + '.json')):
            continue
        st, nf = compile_room(romfs, sc, os.path.join(romfs, 'rooms', sc + '.ssf'))
        total += st
        if len(scenes) < 5:
            print(f'{sc}: {nf} FSMs, {st["kept"]} actions kept')
    skipped = sum(v for k, v in total.items() if k.startswith('skipped'))
    print(f'{total["kept"]} actions kept, {skipped} skipped')
    for k, v in sorted(((k, v) for k, v in total.items() if k.startswith('skipped')), key=lambda x: -x[1])[:15]:
        print(f'  {v:6d} {k}')


if __name__ == '__main__':
    main()
