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
    ('CheckHeroPerformanceRegion', 'Target None'),
    ('RunDialogue', 'Sheet Key Target'),
    ('ActivateInteractible', 'Target Activate'),
    ('FinishFSM', ''),
    ('PlayAudioEvent', 'audioClip'),
]
OP_ID = {n: i for i, (n, _) in enumerate(OPS)}

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
            if t == 'gameobject' and isinstance(value, dict) and 'go' in value:
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
                                            'Time', 'falseValue', 'trueValue', 'gravityScale') and t not in (
                                                'RandomInt', 'IntCompare', 'IntAdd') else None
                    b += val(v, vars_ix, kind)
                stats['kept'] += 1
        b_fsm += b

    b_nodes = bytearray(struct.pack('<H', len(tree)))
    for gid, name, father, x, y, active in tree:
        parent = node_of.get(father, -1)
        b_nodes += struct.pack('<HhhffB3x', S(name), parent, obj_of.get(gid, -1), x, y, active)
    dyn_path = os.path.join(WORK, 'rooms', scene + '.dyn.json')
    dyn = json.load(open(dyn_path)) if os.path.exists(dyn_path) else []
    b_dyn = bytearray(struct.pack('<H', len(dyn)))
    for gid, x0, y0, x1, y1 in dyn:
        b_dyn += struct.pack('<h4f', node_of.get(gid, -1), x0, y0, x1, y1)
    out = b'SSFM' + struct.pack('<H', 2) + S.blob() + bytes(b_nodes) + struct.pack('<H', nf) + bytes(b_fsm) + bytes(b_dyn)
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
