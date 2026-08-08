// Mimi's body.
//
// The VRM ships no animation at all -- VRoid Studio exports geometry, a
// humanoid rig, blendshapes and spring bones, and nothing that moves. So every
// motion here is generated: a rest pose that gets her out of the T-pose she is
// authored in, a pose per presence blended on top of it, and a continuous idle
// layer (breath, weight shift, blink, gaze) underneath everything so she is
// never still. Hair and skirt follow for free -- VRMC_springBone is simulated
// by three-vrm once vrm.update() is called each frame.
//
// The public surface is window.mimiAvatar, driven from C++ through
// QWebChannel. Everything it accepts is a target, never a keyframe: the app
// says what she *is*, this decides what that looks like.

import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { VRMLoaderPlugin, VRMUtils } from '@pixiv/three-vrm';

// ---------------------------------------------------------------- constants

// Matches ui::theme -- the graphite the rest of the app is painted in. Only
// used when the host asks for an opaque background; over the living canvas the
// page is transparent instead.
const kVoid = 0x171718;

// Presence, mirrored from ui/presence.hpp. Strings rather than the enum's
// integers so a mismatch is visible in a log instead of silently posing her
// wrong.
const kPresences = [
    'observing', 'listening', 'thinking', 'speaking', 'remembering', 'muted',
];

// How fast a pose blends in. One number, like theme::kMotionMs -- nothing in
// this app snaps.
const kPoseLerp = 3.2;   // per second, exponential approach
const kGazeLerp = 6.0;
const kExprLerp = 8.0;

// ------------------------------------------------------------------- poses
//
// Every value is a local euler (radians) applied to a *normalized* humanoid
// bone -- three-vrm's normalized rig is a clean T-pose with identity
// rotations, so these read the same for any VRM, not just this model.
//
// Sign conventions, worked out from the rig rather than guessed: the left arm
// points along +X and the right along -X, so rotating about Z by a negative
// angle swings the left arm down and a positive angle does the same on the
// right. Fingers follow the same rule, which is why the curl helper only needs
// the side.

function curl(side, amount) {
    // Left fingers point +X and curl toward the palm at -Y; the right mirrors
    // it. One helper so a fist is a number instead of thirty lines.
    const s = side === 'left' ? -1 : 1;
    const bones = {};
    for (const finger of ['Index', 'Middle', 'Ring', 'Little']) {
        const p = side + finger;
        bones[p + 'Proximal']     = { z: s * amount * 0.9 };
        bones[p + 'Intermediate'] = { z: s * amount * 1.1 };
        bones[p + 'Distal']       = { z: s * amount * 0.8 };
    }
    // The thumb sits on a different axis to the fingers; curling it on Z folds
    // it through the palm, so it gets a gentler rotation of its own.
    bones[side + 'ThumbMetacarpal'] = { y: -s * amount * 0.5, z: s * amount * 0.2 };
    bones[side + 'ThumbProximal']   = { y: -s * amount * 0.4 };
    bones[side + 'ThumbDistal']     = { y: -s * amount * 0.3 };
    return bones;
}

// The rest pose. Applied under every other pose, because the authored T-pose
// is a specification, not a way to stand: without this she greets you like a
// scarecrow.
const kRest = {
    leftShoulder:   { z: -0.10 },
    rightShoulder:  { z:  0.10 },
    leftUpperArm:   { z: -1.19, x: 0.06, y: -0.06 },
    rightUpperArm:  { z:  1.19, x: 0.06, y:  0.06 },
    leftLowerArm:   { z: -0.20, y: -0.28 },
    rightLowerArm:  { z:  0.20, y:  0.28 },
    leftHand:       { z: -0.06 },
    rightHand:      { z:  0.06 },
    ...curl('left', 0.30),
    ...curl('right', 0.30),
    spine:          { x: 0.02 },
    chest:          { x: -0.01 },
    leftUpperLeg:   { z: -0.03 },
    rightUpperLeg:  { z:  0.03 },
};

// One pose per presence, blended on top of the rest pose. These are the
// difference between a model on a turntable and someone who is listening to
// you: the posture says what she is doing before the label under her does.
const kPoses = {
    // Resting and aware. A touch of asymmetry so she does not read as a
    // mannequin standing to attention.
    observing: {
        head:          { y: 0.05, x: 0.02 },
        neck:          { y: 0.03 },
        spine:         { y: 0.02 },
        leftUpperArm:  { z: 0.02 },
        rightUpperArm: { z: 0.03, x: 0.04 },
    },

    // Hearing you. She leans in and comes up to meet your eye -- the whole
    // point is that you know you were heard before you finish the sentence.
    listening: {
        spine:         { x: 0.055 },
        chest:         { x: 0.03 },
        neck:          { x: -0.05 },
        head:          { x: -0.04, z: 0.05 },
        leftUpperArm:  { z: 0.05, x: -0.03 },
        rightUpperArm: { z: -0.05, x: -0.03 },
    },

    // Working it out. Eyes off you and up, weight back, one hand drifting
    // toward her chin -- thinking is the one state where breaking eye contact
    // is the honest signal.
    thinking: {
        spine:          { x: -0.03, y: -0.06 },
        chest:          { y: -0.04 },
        neck:           { x: -0.10, y: -0.10 },
        head:           { x: -0.12, y: -0.12, z: -0.09 },
        // Hand to the chin. The upper arm stays hanging and rotates *forward*
        // rather than lifting sideways -- lifting it swings the forearm across
        // her chest and folds the hand into her own sleeve.
        rightUpperArm:  { z: 0.10, x: -0.65, y: -0.05 },
        rightLowerArm:  { z: -0.10, y: 2.05 },
        rightHand:      { x: 0.15 },
        ...curl('right', 0.55),
        leftUpperArm:   { z: 0.06 },
        leftLowerArm:   { y: -0.35 },
    },

    // Answering. Open posture, back to your eye line; the visemes carry the
    // rest, so the body stays quiet enough not to fight them.
    speaking: {
        spine:         { x: 0.02, y: -0.02 },
        neck:          { x: -0.02 },
        head:          { x: -0.01, y: 0.02 },
        leftUpperArm:  { z: 0.10, x: -0.10 },
        rightUpperArm: { z: -0.10, x: -0.10 },
        leftLowerArm:  { y: -0.20 },
        rightLowerArm: { y: 0.20 },
    },

    // Filing it away. A small settling nod, eyes softened -- the beat after an
    // answer, not a state you catch her in for long.
    remembering: {
        spine:         { x: 0.01 },
        neck:          { x: 0.07 },
        head:          { x: 0.09, y: 0.04 },
        leftUpperArm:  { z: 0.03 },
        rightUpperArm: { z: -0.03 },
    },

    // Switched off. Closed, head down, hands together -- she should look
    // *stood down*, not merely idle, or muting her says nothing.
    muted: {
        spine:          { x: 0.05 },
        chest:          { x: 0.04 },
        neck:           { x: 0.10 },
        head:           { x: 0.14 },
        leftUpperArm:   { z: 0.16, x: -0.22 },
        rightUpperArm:  { z: -0.16, x: -0.22 },
        leftLowerArm:   { y: -0.75 },
        rightLowerArm:  { y: 0.75 },
        ...curl('left', 0.45),
        ...curl('right', 0.45),
    },
};

// ---------------------------------------------------------------- gestures
//
// One-shot motions, added on top of whatever pose is current and evaluated
// from a normalised time u in 0..1. A pose says what she is; a gesture is
// something she *does*, and the difference is most of what stops a character
// from feeling like a screensaver.
//
// Each returns bone deltas. Windowing (sin(pi*u)) keeps them starting and
// ending at zero, so one can fire at any moment without a visible step.

const kGestures = {
    // "Go on, I'm following." Fired while she is being spoken to at length.
    nod: {
        duration: 0.85,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                head: { x: Math.sin(u * Math.PI * 2) * 0.13 * w },
                neck: { x: Math.sin(u * Math.PI * 2) * 0.07 * w },
            };
        },
    },
    // Someone just started talking. A small catch of the breath and a lift --
    // this is the one that makes her feel like she noticed you, and it fires
    // off the onset rather than off the level.
    perk: {
        duration: 0.7,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                head:  { x: -0.09 * w, z: 0.05 * w },
                neck:  { x: -0.05 * w },
                chest: { x: 0.035 * w },
                leftShoulder:  { z: -0.05 * w },
                rightShoulder: { z:  0.05 * w },
            };
        },
    },
    // Cut off mid-sentence. Not a flinch -- she stops, straightens, and gives
    // you the floor, which is the read we want when you talk over her.
    yield: {
        duration: 1.0,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                head:  { x: 0.06 * w, y: -0.05 * w },
                neck:  { x: 0.05 * w },
                spine: { x: -0.03 * w },
                leftUpperArm:  { z: 0.05 * w },
                rightUpperArm: { z: -0.05 * w },
            };
        },
    },
    // Shifting her weight, the way anyone standing for a while does.
    shift: {
        duration: 2.4,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                hips:  { y: 0.10 * w, z: 0.05 * w },
                spine: { y: -0.05 * w },
                head:  { y: 0.04 * w },
                leftUpperArm:  { z: 0.05 * w },
                rightUpperArm: { z: -0.03 * w },
            };
        },
    },
    // Glancing away and back. Nobody holds eye contact with an empty room.
    glance: {
        duration: 2.0,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                head: { y: -0.30 * w, x: -0.04 * w },
                neck: { y: -0.14 * w },
                spine: { y: -0.05 * w },
            };
        },
    },
    // A small stretch, for when she has been idle a long time.
    stretch: {
        duration: 2.8,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                spine: { x: -0.09 * w },
                chest: { x: -0.06 * w },
                neck:  { x: -0.10 * w },
                head:  { x: -0.08 * w, z: 0.06 * w },
                leftUpperArm:  { z: 0.28 * w, x: -0.14 * w },
                rightUpperArm: { z: -0.24 * w, x: -0.12 * w },
                leftLowerArm:  { y: -0.30 * w },
                rightLowerArm: { y: 0.30 * w },
            };
        },
    },
    // Beats, for while she is talking.
    //
    // People gesture *on* the stresses of their own speech, not continuously,
    // so these are short and fired from the viseme track rather than run on a
    // timer -- which is what keeps them looking like emphasis instead of
    // fidgeting.
    beatNod: {
        duration: 0.7,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return { head: { x: 0.07 * w, y: -0.03 * w }, neck: { x: 0.04 * w } };
        },
    },
    beatHand: {
        duration: 1.1,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                rightUpperArm: { z: -0.30 * w, x: -0.28 * w },
                rightLowerArm: { y: 0.55 * w, z: -0.12 * w },
                rightHand:     { x: -0.18 * w },
                chest:         { y: -0.04 * w },
            };
        },
    },
    beatOpen: {
        duration: 1.2,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return {
                leftUpperArm:  { z: 0.26 * w, x: -0.24 * w },
                rightUpperArm: { z: -0.26 * w, x: -0.24 * w },
                leftLowerArm:  { y: -0.42 * w },
                rightLowerArm: { y: 0.42 * w },
                spine:         { x: 0.02 * w },
            };
        },
    },
    beatTilt: {
        duration: 0.9,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return { head: { z: 0.10 * w, y: 0.07 * w }, neck: { z: 0.04 * w } };
        },
    },
    // Curiosity, for a sound she cannot place.
    tilt: {
        duration: 1.1,
        pose: u => {
            const w = Math.sin(Math.PI * u);
            return { head: { z: 0.16 * w, y: 0.06 * w }, neck: { z: 0.06 * w } };
        },
    },
};

// The face that goes with each posture. Kept low -- a VRM expression at 1.0 is
// a caricature, and she has to hold these for minutes at a time.
const kExpressions = {
    observing:   { relaxed: 0.15 },
    listening:   { happy: 0.12 },
    thinking:    { relaxed: 0.35 },
    speaking:    { happy: 0.10 },
    remembering: { happy: 0.30 },
    muted:       { sad: 0.18 },
};

// Where she looks in each state. Thinking is the only one that breaks eye
// contact, which is what makes it read as thought rather than inattention.
const kGazeBias = {
    observing:   { x: 0.00, y: 0.00 },
    listening:   { x: 0.00, y: 0.02 },
    thinking:    { x: -0.35, y: 0.30 },
    speaking:    { x: 0.00, y: 0.01 },
    remembering: { x: 0.10, y: -0.20 },
    muted:       { x: 0.00, y: -0.28 },
};

// VOICEVOX mora vowels -> VRM viseme expressions. 'N' (ん) and silence close
// the mouth, which is why they map to nothing rather than to a shape.
const kVisemeFor = { a: 'aa', i: 'ih', u: 'ou', e: 'ee', o: 'oh' };
const kVisemes = ['aa', 'ih', 'ou', 'ee', 'oh'];

// ------------------------------------------------------------------ helpers

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
// Frame-rate independent exponential approach. The naive `a += (b-a)*k` is
// tied to frame rate and drifts between a 120 Hz laptop panel and a 60 Hz
// external display.
const approach = (a, b, rate, dt) => a + (b - a) * (1 - Math.exp(-rate * dt));

// Cheap smooth noise: a few incommensurable sines. Enough to keep the idle
// layer from looking looped without shipping a noise implementation.
function wobble(t, seed) {
    return (Math.sin(t * 0.71 + seed) * 0.6 +
            Math.sin(t * 1.13 + seed * 2.3) * 0.3 +
            Math.sin(t * 0.31 + seed * 5.1) * 0.1);
}

// ------------------------------------------------------------------- avatar

class Avatar {
    constructor(canvasHost) {
        this.host = canvasHost;
        this.vrm = null;
        this.clock = new THREE.Clock();

        this.presence = 'observing';
        this.gazeTarget = { x: 0, y: 0 };   // -1..1, where the user is
        this.gaze = { x: 0, y: 0 };

        // Listening, as something she does rather than a state she sits in.
        //
        // One smoothed level is enough to know that someone is talking and not
        // enough to react to *how*. Three signals are: a fast envelope that
        // follows the shape of a phrase, a slow one that is the room's own
        // loudness, and the difference between them, which is an onset -- the
        // moment a voice starts, or gets suddenly louder. People visibly react
        // to onsets and barely react to steady sound, so that is what drives
        // the startle and the perk-up.
        this.level = 0;      // fast envelope, 0..1
        this.floor = 0;      // slow envelope: the room
        this.onset = 0;      // decaying impulse, 0..1
        this.speechHeld = 0; // seconds of continuous speech, for the nods
        this.sinceNod = 0;
        this.sinceIdle = 0;
        this.nextIdle = 4;
        this.sinceBeat = 0;
        this.nextBeat = 0.8;

        // Current blended pose: bone -> {x,y,z} actually applied this frame.
        this.pose = {};
        this.exprWeights = {};

        // Blink state machine. Randomised, because a metronome blink is
        // uncanny in a way that no blink at all is not.
        this.blink = 0;
        this.nextBlink = 1.5 + Math.random() * 3;
        this.blinkPhase = 0;

        // Viseme schedule: {t, viseme, weight} sorted by t, played against a
        // start timestamp. Empty when she is not speaking.
        this.visemeTrack = [];
        this.visemeStart = 0;
        this.mouth = {};           // per-viseme current weights

        // The gesture in flight, if any. One at a time: two overlapping
        // one-shots on the same bones read as a twitch.
        this.gesture = null;       // {name, t}

        this.#initScene();
    }

    #initScene() {
        const renderer = new THREE.WebGLRenderer({
            antialias: true,
            alpha: true,
            powerPreference: 'low-power',   // she is on screen all day
        });
        renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
        renderer.outputColorSpace = THREE.SRGBColorSpace;
        // MToon is authored against unlit, flat colour. Tone mapping would
        // desaturate exactly the flat fills that make it read as anime.
        renderer.toneMapping = THREE.NoToneMapping;
        this.host.appendChild(renderer.domElement);
        this.renderer = renderer;

        const scene = new THREE.Scene();
        scene.background = null;   // the Qt canvas shows through
        this.scene = scene;

        // Toon shading wants a soft, dominant key and a lot of fill: a hard
        // three-point rig carves shadow shapes into a model that has none
        // painted in.
        scene.add(new THREE.AmbientLight(0xffffff, 1.60));
        const key = new THREE.DirectionalLight(0xfff4e8, 1.25);
        key.position.set(0.6, 1.6, 1.4);
        scene.add(key);
        // A cool rim in the app's accent, so she sits in the same light as the
        // rest of the interface instead of being pasted onto it.
        const rim = new THREE.DirectionalLight(0x3b82f6, 0.55);
        rim.position.set(-1.2, 0.8, -1.0);
        scene.add(rim);
        this.rim = rim;

        // Full body, standing in her own column down the right of the window.
        // A cropped bust reads as a portrait pasted into a page; a whole figure
        // reads as someone in the room, which is the entire point of giving her
        // one. The model is 1.612 m tall with its feet at y=0.
        //
        // Long lens, held back: 22° at ~4.9 m rather than a wide angle up
        // close, because perspective distortion at conversational distance is
        // what makes a 3D character look like a game asset instead of a person.
        this.camera = new THREE.PerspectiveCamera(22, 1, 0.1, 40);
        // She stands right of centre, and the canvas is the whole window --
        // there is no panel for her to be boxed inside. Offsetting the camera
        // rather than moving the model keeps her at the world origin, so every
        // pose, the spring bones and the ground light stay where they were.
        this.home = {
            position: new THREE.Vector3(-0.62, 0.98, 4.42),
            target: new THREE.Vector3(-0.62, 0.82, 0),
        };
        this.camera.position.copy(this.home.position);

        // You can walk around her.
        //
        // A character you can only see from one angle is a picture of a
        // character. Being able to lean in, walk round and look up at her is
        // most of what makes her feel like she occupies space rather than
        // decorating a panel -- so: drag to orbit, scroll to close the
        // distance, right-drag to pan, double-click to go back to the framing
        // the app composed.
        const controls = new OrbitControls(this.camera, this.renderer.domElement);
        controls.target.copy(this.home.target);
        // Damped, because the whole app moves on one curve and a camera that
        // stops dead is the one thing that would not.
        controls.enableDamping = true;
        controls.dampingFactor = 0.075;
        controls.rotateSpeed = 0.55;
        controls.zoomSpeed = 0.8;
        controls.panSpeed = 0.6;
        // Close enough to read her face, far enough to see all of her.
        controls.minDistance = 0.85;
        controls.maxDistance = 7.5;
        // Horizontally unrestricted -- a full 360 around her, which is the
        // point of being able to move at all. Vertically clamped at the
        // horizon: maxPolarAngle 1.50 rad is just under 90 degrees, so the
        // camera can never drop below her chest and look up at her. That was
        // not a deliberate feature, it was 1.62 being slightly past the
        // horizontal, and a camera that can get under a standing figure is a
        // defect in a product that sits on a desk at work.
        controls.minPolarAngle = 0.30;
        controls.maxPolarAngle = 1.50;
        controls.screenSpacePanning = true;
        this.controls = controls;

        this.renderer.domElement.addEventListener('dblclick', () => this.resetView());

        // The model faces +Z (checked against the rig: the eye bones sit at
        // z=+0.023 in front of the head), and three.js looks down -Z from the
        // camera, so she already faces the viewer. VRM 0.x would need a 180°
        // turn here; this is VRM 1.0.
        //
        // She is turned a little away from square-on. Standing dead-on to the
        // camera is the one pose that reads as a menu screen; a three-quarter
        // body with the head still coming back to you is how a person stands
        // when someone is talking to them. Negative Y turns her toward screen
        // left, which is where the conversation is.
        this.bodyTurn = -0.34;

        this.lookTarget = new THREE.Object3D();
        this.lookTarget.position.set(0, 1.46, 4.42);
        scene.add(this.lookTarget);

        this.resize();
        new ResizeObserver(() => this.resize()).observe(this.host);
    }

    resize() {
        const w = Math.max(1, this.host.clientWidth);
        const h = Math.max(1, this.host.clientHeight);
        // updateStyle left on (the third argument defaults true). Passing false
        // resizes the drawing buffer but leaves the canvas element at its
        // default 300x150 CSS size in the top-left corner -- which, once the
        // canvas became the whole window, is a patch of empty background. She
        // was being rendered the whole time, into a box you could not see.
        this.renderer.setSize(w, h);
        this.camera.aspect = w / h;
        this.camera.updateProjectionMatrix();
    }

    async load(url) {
        const loader = new GLTFLoader();
        loader.register(p => new VRMLoaderPlugin(p));
        const gltf = await loader.loadAsync(url);
        const vrm = gltf.userData.vrm;

        // Both are pure wins on a VRoid export: it ships one skinned mesh per
        // hair strand (88 of them here), and the unused-vertex pass drops what
        // the morph targets never touch.
        VRMUtils.removeUnnecessaryVertices(gltf.scene);
        VRMUtils.combineSkeletons(gltf.scene);

        // Nothing casts or receives -- there are no shadows in this look, and
        // leaving it on costs a depth pass for nothing.
        vrm.scene.traverse(o => { o.frustumCulled = false; });

        if (vrm.lookAt) {
            vrm.lookAt.target = this.lookTarget;
            // Bone-driven lookAt on this model moves the eyes only; the head
            // follow is ours, so it can be damped and limited.
            vrm.lookAt.autoUpdate = true;
        }

        vrm.scene.rotation.y = this.bodyTurn;
        this.scene.add(vrm.scene);
        this.vrm = vrm;

        // Snap straight to the rest pose. Easing into it from the authored
        // T-pose would mean her first second on screen is a scarecrow lowering
        // its arms, which is exactly the frame the fade-in is meant to hide.
        for (const [bone, euler] of Object.entries(kRest)) {
            const node = vrm.humanoid.getNormalizedBoneNode(bone);
            if (!node) continue;
            const cur = { x: euler.x || 0, y: euler.y || 0, z: euler.z || 0 };
            this.pose[bone] = cur;
            node.rotation.set(cur.x, cur.y, cur.z);
        }
        vrm.update(0);
        return vrm;
    }

    // ---------------------------------------------------------------- public

    setPresence(presence) {
        const p = String(presence || '').toLowerCase();
        if (!kPresences.includes(p)) return;
        if (p === this.presence) return;
        const was = this.presence;
        this.presence = p;
        // A new posture is a good moment to blink -- people do it when they
        // change what they are attending to.
        this.nextBlink = Math.min(this.nextBlink, 0.35);

        // Cut off mid-answer: she stops and hands the floor back. Nothing else
        // on screen says "I heard you interrupt me" as clearly as her doing it
        // with her body.
        if (was === 'speaking' && p === 'listening') this.playGesture('yield');
        if (was !== 'listening' && p === 'listening') this.speechHeld = 0;
    }

    // Back to the framing the app composed. Bound to double-click, and exposed
    // so a menu item or a keystroke in the host can do the same.
    resetView() {
        if (!this.controls) return;
        this.camera.position.copy(this.home.position);
        this.controls.target.copy(this.home.target);
        this.controls.update();
    }

    // Fire a one-shot motion. Ignored if one is already running, so a burst of
    // triggers cannot stack into a spasm.
    playGesture(name) {
        if (!kGestures[name]) return;
        if (this.gesture !== null) return;
        this.gesture = { name, t: 0 };
    }

    setLevel(rms) {
        const v = clamp(Number(rms) || 0, 0, 1);

        // Fast up, slow down. The raw 80 ms RMS jitters hard enough to look
        // like a fault (the 2D orb found the same), but smoothing it evenly
        // also smears away the attack of a syllable -- which is the part worth
        // reacting to. So: catch the rise almost immediately, let the fall
        // take its time.
        this.level = v > this.level ? this.level * 0.35 + v * 0.65
                                    : this.level * 0.88 + v * 0.12;
        // The room, over a couple of seconds. Whatever is always there --
        // a fan, the street -- settles into this and stops being interesting.
        this.floor = this.floor * 0.985 + v * 0.015;

        // Speech is what stands out from the room, not what is loud in
        // absolute terms; a quiet voice in a quiet room should still land.
        const excess = this.level - Math.max(this.floor * 1.5, 0.012);
        if (excess > 0.02) this.onset = Math.min(1, Math.max(this.onset, excess * 5));
    }

    // -1..1 in each axis, where the user's attention is. The app feeds it the
    // cursor position relative to the widget, so she reads as looking at what
    // you are doing rather than past you.
    setGaze(x, y) {
        this.gazeTarget.x = clamp(Number(x) || 0, -1, 1);
        this.gazeTarget.y = clamp(Number(y) || 0, -1, 1);
    }

    // The whole point of driving lip sync from VOICEVOX: `track` is
    // [{t, vowel, length}] in seconds, straight out of the mora timings in the
    // audio query, so the mouth is right by construction instead of chasing
    // the waveform. `delay` covers the gap between this call and the first
    // sample actually leaving the speaker.
    playVisemes(track, delay = 0) {
        if (!Array.isArray(track)) return;
        this.visemeTrack = track
            .map(m => ({
                t: Number(m.t) || 0,
                length: Math.max(0.03, Number(m.length) || 0.08),
                viseme: kVisemeFor[String(m.vowel || '').toLowerCase()] || null,
            }))
            .sort((a, b) => a.t - b.t);
        this.visemeStart = performance.now() / 1000 + (Number(delay) || 0);
        // First beat lands just after she starts, not on the opening syllable.
        this.sinceBeat = 0;
        this.nextBeat = 0.7;
    }

    // Barge-in and Stop both land here: the mouth has to close the instant the
    // audio is cut, or she carries on mouthing an answer nobody can hear.
    clearVisemes() {
        this.visemeTrack = [];
    }

    // ---------------------------------------------------------------- frame

    // Build this frame's target pose: rest + the presence pose + the idle
    // layer, then ease every bone toward it. Composing targets and easing once
    // is what keeps a presence change from snapping.
    #poseFrame(t, dt) {
        const target = {};
        const add = (bone, e) => {
            const b = target[bone] || (target[bone] = { x: 0, y: 0, z: 0 });
            b.x += e.x || 0; b.y += e.y || 0; b.z += e.z || 0;
        };

        for (const [bone, e] of Object.entries(kRest)) add(bone, e);
        for (const [bone, e] of Object.entries(kPoses[this.presence] || {})) add(bone, e);

        // Breath. Slower and deeper when she is idle, quicker while speaking --
        // the cheapest cue that there is something alive in there.
        const breathRate = this.presence === 'speaking' ? 2.1
                         : this.presence === 'listening' ? 1.5 : 1.05;
        const breath = Math.sin(t * breathRate) * 0.5 + 0.5;
        add('spine',  { x: breath * 0.016 });
        add('chest',  { x: breath * 0.012 });
        add('upperChest', { x: breath * 0.008 });
        add('leftShoulder',  { z: -breath * 0.02 });
        add('rightShoulder', { z:  breath * 0.02 });

        // Weight shift. A very slow sway through the hips and spine so she
        // never holds one silhouette long enough for it to read as a freeze.
        const sway = wobble(t * 0.25, 1.7);
        add('hips',  { y: sway * 0.045, z: sway * 0.012 });
        add('spine', { y: -sway * 0.02 });
        add('leftUpperArm',  { z: sway * 0.035 });
        add('rightUpperArm', { z: sway * 0.035 });

        // Micro-motion in the neck, independent of the sway so the head is
        // never perfectly locked to the body.
        add('neck', { x: wobble(t * 0.4, 4.2) * 0.015, y: wobble(t * 0.33, 9.1) * 0.02 });

        // Hearing you moves her. Reacting to the *shape* of what you say --
        // leaning in as a phrase builds, settling back in the gaps -- is what
        // separates listening from waiting, and it is why the envelope is fast
        // on the way up and slow on the way down.
        if (this.presence === 'listening' || this.presence === 'observing') {
            const attention = this.presence === 'listening' ? 1.0 : 0.35;
            const v = this.level * attention;
            add('neck',  { x: -v * 0.13 });
            add('chest', { x: v * 0.06 });
            add('spine', { x: v * 0.04 });
            // She turns very slightly into the sound.
            add('head',  { x: -v * 0.05, y: this.onset * 0.04 });
            add('leftShoulder',  { z: -v * 0.03 });
            add('rightShoulder', { z:  v * 0.03 });
        }

        // The gesture layer, on top of everything.
        if (this.gesture !== null) {
            const spec = kGestures[this.gesture.name];
            const u = clamp(this.gesture.t / spec.duration, 0, 1);
            for (const [bone, e] of Object.entries(spec.pose(u))) add(bone, e);
        }

        // The head follows the gaze, but only part way and only so far: eyes
        // do most of the work, and a head that tracks one-to-one reads as a
        // security camera.
        add('neck', { y: this.gaze.x * 0.13, x: -this.gaze.y * 0.10 });
        add('head', { y: this.gaze.x * 0.20, x: -this.gaze.y * 0.14, z: this.gaze.x * 0.05 });

        // Ease every bone toward the composed target.
        const rate = kPoseLerp;
        for (const [bone, e] of Object.entries(target)) {
            const node = this.vrm.humanoid.getNormalizedBoneNode(bone);
            if (!node) continue;
            const cur = this.pose[bone] || (this.pose[bone] = { x: 0, y: 0, z: 0 });
            cur.x = approach(cur.x, e.x, rate, dt);
            cur.y = approach(cur.y, e.y, rate, dt);
            cur.z = approach(cur.z, e.z, rate, dt);
            node.rotation.set(cur.x, cur.y, cur.z);
        }
        // Bones the current pose stopped mentioning have to be eased back to
        // neutral, or a hand raised while thinking would stay up for ever.
        for (const [bone, cur] of Object.entries(this.pose)) {
            if (target[bone]) continue;
            const node = this.vrm.humanoid.getNormalizedBoneNode(bone);
            if (!node) continue;
            cur.x = approach(cur.x, 0, rate, dt);
            cur.y = approach(cur.y, 0, rate, dt);
            cur.z = approach(cur.z, 0, rate, dt);
            node.rotation.set(cur.x, cur.y, cur.z);
        }
    }

    // Decides what to *do* about the sound, once a frame. Kept apart from the
    // pose so the reactions are one readable list rather than conditions
    // scattered through the skeleton code.
    #reactFrame(dt) {
        // Onsets are impulses; they have to die away or every reaction latches.
        this.onset = Math.max(0, this.onset - dt * 2.6);

        if (this.gesture !== null) {
            this.gesture.t += dt;
            if (this.gesture.t >= kGestures[this.gesture.name].duration) this.gesture = null;
        }
        this.sinceNod += dt;

        const speaking_to_her = this.level > 0.045;
        this.speechHeld = speaking_to_her ? this.speechHeld + dt : 0;

        if (this.presence === 'listening') {
            // A voice arriving after a gap: she catches it and comes up to
            // meet you. The gate is on the onset, not the level, so a steady
            // loud room does not have her perking every second.
            if (this.onset > 0.55 && this.speechHeld < 0.4) this.playGesture('perk');
            // Still going: an occasional nod, the way people do to signal they
            // are following without taking the floor.
            if (this.speechHeld > 1.6 && this.sinceNod > 3.2) {
                this.playGesture('nod');
                this.sinceNod = 0;
            }
        } else if (this.presence === 'speaking') {
            // Gesture while she talks. Timed off her own speech: a beat lands
            // shortly after a mora does, so the movement falls on what she is
            // saying rather than arriving at random.
            this.sinceBeat += dt;
            const talking = this.visemeTrack.length > 0;
            if (talking && this.sinceBeat > this.nextBeat) {
                const beats = ['beatNod', 'beatHand', 'beatNod', 'beatOpen', 'beatTilt'];
                this.playGesture(beats[Math.floor(Math.random() * beats.length)]);
                this.sinceBeat = 0;
                this.nextBeat = 1.6 + Math.random() * 2.2;
            }
        } else if (this.presence === 'observing') {
            // A sound she was not expecting, while resting.
            if (this.onset > 0.8 && this.sinceNod > 6) {
                this.playGesture('tilt');
                this.sinceNod = 0;
                this.sinceIdle = 0;
                return;
            }
            // Otherwise: live. Standing perfectly still between questions is
            // the single thing that gives away that she is a model rather than
            // a person waiting -- the breath and the sway are too subtle to
            // carry a whole minute on their own, so every few seconds she does
            // something small and deliberate.
            this.sinceIdle += dt;
            if (this.sinceIdle > this.nextIdle) {
                const moves = ['glance', 'shift', 'glance', 'shift', 'stretch', 'tilt'];
                this.playGesture(moves[Math.floor(Math.random() * moves.length)]);
                this.sinceIdle = 0;
                // Irregular on purpose: anything on a fixed period reads as a
                // loop within about three repetitions.
                this.nextIdle = 5 + Math.random() * 7;
            }
        } else {
            this.sinceIdle = 0;
        }
    }

    #blinkFrame(dt) {
        this.nextBlink -= dt;
        if (this.nextBlink <= 0 && this.blinkPhase === 0) {
            this.blinkPhase = 1;
            // Human blink spacing is bursty, not uniform: mostly a few seconds
            // apart with the occasional double.
            this.nextBlink = Math.random() < 0.18 ? 0.28 : 2.2 + Math.random() * 4.0;
        }
        if (this.blinkPhase > 0) {
            // 60 ms shut, 110 ms open -- fast enough that you feel it rather
            // than watch it.
            this.blinkPhase += dt / (this.blink < 0.999 && this.blinkPhase < 2 ? 0.06 : 0.11);
            if (this.blinkPhase < 2) {
                this.blink = Math.min(1, this.blinkPhase);
                if (this.blink >= 1) this.blinkPhase = 2;
            } else {
                this.blink -= dt / 0.11;
                if (this.blink <= 0) { this.blink = 0; this.blinkPhase = 0; }
            }
        }
        // Thinking half-lids the eyes; muted lowers them further. Both stack
        // with the blink rather than fighting it.
        const lid = this.presence === 'thinking' ? 0.22
                  : this.presence === 'muted' ? 0.45
                  : this.presence === 'remembering' ? 0.35 : 0;
        return Math.max(this.blink, lid);
    }

    #mouthFrame(t, dt) {
        // Which viseme should be open right now, from the schedule.
        let active = null;
        if (this.visemeTrack.length > 0) {
            const now = performance.now() / 1000 - this.visemeStart;
            if (now >= 0) {
                for (const m of this.visemeTrack) {
                    if (now >= m.t && now < m.t + m.length) { active = m; break; }
                    if (m.t > now) break;
                }
                const last = this.visemeTrack[this.visemeTrack.length - 1];
                if (now > last.t + last.length + 0.25) this.visemeTrack = [];
            }
        }

        for (const v of kVisemes) {
            const want = active && active.viseme === v ? 0.85 : 0;
            const cur = this.mouth[v] || 0;
            // Opening faster than closing keeps consonants crisp; the reverse
            // makes her sound like she is chewing.
            const rate = want > cur ? 26 : 15;
            this.mouth[v] = approach(cur, want, rate, dt);
            this.vrm.expressionManager?.setValue(v, this.mouth[v]);
        }
    }

    #expressionFrame(dt) {
        const want = { ...(kExpressions[this.presence] || {}) };

        // Her face reacts too, not just her posture. A phrase landing widens
        // the eyes a little; the harder you come in, the more it shows. Small
        // numbers on purpose -- at any real weight this stops being attention
        // and becomes alarm.
        if (this.presence === 'listening') {
            want.surprised = (want.surprised || 0) + this.onset * 0.22;
            want.happy = (want.happy || 0) + this.level * 0.18;
        }

        for (const name of ['happy', 'angry', 'sad', 'relaxed', 'surprised']) {
            const cur = this.exprWeights[name] || 0;
            // Surprise has to arrive quickly to read as a reaction at all;
            // everything else moves at the app's one pace.
            const rate = name === 'surprised' ? 16 : kExprLerp;
            this.exprWeights[name] = approach(cur, clamp(want[name] || 0, 0, 1), rate, dt);
            this.vrm.expressionManager?.setValue(name, this.exprWeights[name]);
        }
    }

    update() {
        const dt = Math.min(this.clock.getDelta(), 0.1);   // a hidden tab must not teleport her
        const t = this.clock.elapsedTime;
        if (this.vrm) {
            this.#reactFrame(dt);
            // Gaze, biased by what she is doing. Thinking looks away; the rest
            // hold your eye.
            const bias = kGazeBias[this.presence] || { x: 0, y: 0 };
            this.gaze.x = approach(this.gaze.x, this.gazeTarget.x * 0.7 + bias.x, kGazeLerp, dt);
            this.gaze.y = approach(this.gaze.y, this.gazeTarget.y * 0.7 + bias.y, kGazeLerp, dt);
            this.lookTarget.position.set(
                this.gaze.x * 0.8, 1.46 + this.gaze.y * 0.5, 1.15);

            this.#poseFrame(t, dt);
            this.#expressionFrame(dt);
            this.#mouthFrame(t, dt);
            const lids = this.#blinkFrame(dt);
            this.vrm.expressionManager?.setValue('blink', lids);

            // The rim light carries state the way the 2D orb carried it in
            // colour: brighter when she is engaged, almost out when muted.
            const rimWant = this.presence === 'muted' ? 0.12
                          : this.presence === 'speaking' ? 0.85
                          : this.presence === 'listening' ? 0.7 + this.level * 0.5 : 0.5;
            this.rim.intensity = approach(this.rim.intensity, rimWant, 4, dt);

            // Runs the humanoid, the expressions and the spring bones -- the
            // hair and the tie follow the motion above for free.
            this.vrm.update(dt);
        }
        if (this.controls) this.controls.update();
        this.renderer.render(this.scene, this.camera);
    }

    start() {
        const loop = () => { requestAnimationFrame(loop); this.update(); };
        loop();
    }
}

// ------------------------------------------------------------------- wiring

const host = document.getElementById('stage');
const avatar = new Avatar(host);

// The bridge the C++ side talks to. Assigned before the model finishes
// loading, and every method is safe to call early, so AvatarView never has to
// wait for a ready signal before pushing presence at it.
window.mimiAvatar = {
    ready: false,
    setPresence: p => avatar.setPresence(p),
    setLevel:    v => avatar.setLevel(v),
    setGaze:     (x, y) => avatar.setGaze(x, y),
    playVisemes: (track, delay) => avatar.playVisemes(track, delay),
    clearVisemes: () => avatar.clearVisemes(),
    // For the pose-tuning harness only; the app never touches these. Poses are
    // eased toward every frame, so editing the table live re-poses her in
    // place -- which is the whole reason tuning does not need a rebuild.
    _avatar: avatar,
    _poses: kPoses,
    _rest: kRest,
};

// Cursor tracking works inside the view; the app supplies gaze from outside it
// as well, so she keeps following the pointer once it leaves her.
// Hovering moves her gaze; dragging moves the camera. Without the button
// check she also tries to follow the pointer while you orbit, which reads as
// her head being dragged around by the mouse.
addEventListener('pointermove', e => {
    if (e.buttons !== 0) return;
    const r = host.getBoundingClientRect();
    avatar.setGaze((e.clientX - r.left) / r.width * 2 - 1,
                   -((e.clientY - r.top) / r.height * 2 - 1));
});

const params = new URLSearchParams(location.search);
const modelUrl = params.get('model') || 'model.vrm';
if (params.get('bg')) {
    document.body.style.background = '#' + kVoid.toString(16).padStart(6, '0');
}

avatar.start();
avatar.load(modelUrl).then(() => {
    window.mimiAvatar.ready = true;
    document.body.classList.add('ready');
    // The host watches for this to swap the 2D orb out only once there is
    // something to swap in.
    console.log('mimi-avatar: ready');
}).catch(err => {
    console.error('mimi-avatar: load failed', err);
    document.body.classList.add('failed');
});
