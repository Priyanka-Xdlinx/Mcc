import { useEffect, useEffectEvent, useRef, useState } from "react";
import * as THREE from "three";
import { Line2 } from "three/examples/jsm/lines/Line2.js";
import { LineGeometry } from "three/examples/jsm/lines/LineGeometry.js";
import { LineMaterial } from "three/examples/jsm/lines/LineMaterial.js";
import earthDay from "../assets/earth-day.jpg";
import earthNight from "../assets/earth-night.jpg";
import satelliteImg from "../assets/satellite.png";
import logger from "../../../utils/logger.js";
import { footprint, llaToXyz, subsolar } from "../utils/geo.js";

const SAT_ASPECT = 239 / 305;
const HOME_DIST = 3.4;

const EARTH_VERTEX = `varying vec2 vUv; varying vec3 vN;
  void main(){ vUv=uv; vN=normalize(mat3(modelMatrix)*normal);
  gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.0); }`;
const EARTH_FRAGMENT = `uniform sampler2D dayT,nightT; uniform vec3 sunDir; varying vec2 vUv; varying vec3 vN;
  void main(){ float d=dot(normalize(vN),normalize(sunDir));
    float m=smoothstep(-0.12,0.22,d);
    vec3 day=texture2D(dayT,vUv).rgb*(0.34+0.76*max(d,0.0));
    vec3 night=pow(texture2D(nightT,vUv).rgb,vec3(0.85))*1.5;
    vec3 col=mix(night,day,m)+vec3(0.02,0.05,0.10)*(1.0-m)*0.65;
    col+=vec3(0.35,0.16,0.05)*exp(-pow(d*9.0,2.0))*0.35;
    gl_FragColor=vec4(col,1.0); }`;
const ATMO_VERTEX = `varying vec3 vN; varying vec3 vV; varying vec3 vW;
  void main(){ vN=normalize(normalMatrix*normal); vW=normalize(mat3(modelMatrix)*normal);
  vec4 mv=modelViewMatrix*vec4(position,1.0); vV=normalize(-mv.xyz); gl_Position=projectionMatrix*mv; }`;
const ATMO_FRAGMENT = `uniform vec3 sunDir; varying vec3 vN; varying vec3 vV; varying vec3 vW;
  void main(){ float f=pow(1.0-abs(dot(vN,vV)),2.4);
  float lit=0.45+0.55*smoothstep(-0.3,0.6,dot(vW,normalize(sunDir)));
  gl_FragColor=vec4(0.28,0.64,1.0,1.0)*f*lit*1.1; }`;

/** Whether this browser will draw WebGL at all (it can be turned off). */
function hasWebGl() {
  try {
    const c = document.createElement("canvas");
    return Boolean(c.getContext("webgl2") ?? c.getContext("webgl"));
  } catch {
    return false;
  }
}

/** A soft radial glow, drawn once to a canvas, for the spacecraft halo. */
function glowTexture(rgb) {
  const s = 128;
  const c = document.createElement("canvas");
  c.width = c.height = s;
  const g = c.getContext("2d");
  const grad = g.createRadialGradient(s / 2, s / 2, 0, s / 2, s / 2, s / 2);
  grad.addColorStop(0, `rgba(${rgb},0.9)`);
  grad.addColorStop(0.25, `rgba(${rgb},0.45)`);
  grad.addColorStop(1, `rgba(${rgb},0)`);
  g.fillStyle = grad;
  g.fillRect(0, 0, s, s);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

/** A random star field on a far sphere. */
function stars(n, size, opacity) {
  const pos = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) {
    const u = Math.random() * 2 - 1;
    const a = Math.random() * Math.PI * 2;
    const r = 60 + Math.random() * 20;
    const s = Math.sqrt(1 - u * u);
    pos.set([r * s * Math.cos(a), r * u, r * s * Math.sin(a)], i * 3);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.BufferAttribute(pos, 3));
  return new THREE.Points(g, new THREE.PointsMaterial({ color: 0xb8cadb, size, sizeAttenuation: true, transparent: true, opacity }));
}

/** Colours fading from `floor` (oldest) to full (newest) along a trail. */
function fadeColors(n, rgb, floor) {
  const col = [];
  for (let i = 0; i < n; i++) {
    const a = n > 1 ? floor + (1 - floor) * Math.pow(i / (n - 1), 1.6) : 1;
    col.push(rgb[0] * a, rgb[1] * a, rgb[2] * a);
  }
  return col;
}

/**
 * The spacecraft on a day/night Earth, from live GPS telemetry: orbit trail at
 * altitude, ground track on the surface, nadir line, pulsing sub-satellite ring
 * and visibility footprint. Drag to turn, scroll to zoom. The scene is built
 * once; new data reaches it through `api`.
 */
export default function Globe({ track, current, follow, onManual, resetKey }) {
  const stage = useRef(null);
  const canvas = useRef(null);
  const [noGl] = useState(() => !hasWebGl());
  const api = useRef(null);
  // Called from the canvas's own pointer handler, so always the latest prop.
  const turnedByHand = useEffectEvent(() => onManual?.());

  useEffect(() => {
    const el = canvas.current;
    const box = stage.current;
    if (!el || !box || noGl) return undefined;
    let renderer;
    try {
      renderer = new THREE.WebGLRenderer({ canvas: el, antialias: true });
    } catch (e) {
      logger.warn("Globe: WebGL renderer failed", e);
      return undefined;
    }
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    const reduceMotion = window.matchMedia?.("(prefers-reduced-motion: reduce)").matches ?? false;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x02050a);
    const camera = new THREE.PerspectiveCamera(34, 1, 0.01, 200);
    scene.add(stars(1800, 0.07, 0.65), stars(260, 0.16, 0.95));

    const loader = new THREE.TextureLoader();
    const dayT = loader.load(earthDay);
    const nightT = loader.load(earthNight);
    dayT.colorSpace = nightT.colorSpace = THREE.SRGBColorSpace;
    dayT.anisotropy = nightT.anisotropy = 8;
    const earthMat = new THREE.ShaderMaterial({
      uniforms: { dayT: { value: dayT }, nightT: { value: nightT }, sunDir: { value: new THREE.Vector3(1, 0, 0) } },
      vertexShader: EARTH_VERTEX,
      fragmentShader: EARTH_FRAGMENT,
    });
    scene.add(new THREE.Mesh(new THREE.SphereGeometry(1, 160, 100), earthMat));

    // Atmosphere: a blue rim, brighter on the sunlit limb.
    const atmoMat = new THREE.ShaderMaterial({
      side: THREE.BackSide,
      transparent: true,
      depthWrite: false,
      blending: THREE.AdditiveBlending,
      uniforms: { sunDir: { value: new THREE.Vector3(1, 0, 0) } },
      vertexShader: ATMO_VERTEX,
      fragmentShader: ATMO_FRAGMENT,
    });
    scene.add(new THREE.Mesh(new THREE.SphereGeometry(1.045, 96, 64), atmoMat));

    // Thick lines (Line2) so the trails read at any screen density.
    const orbitMat = new LineMaterial({ linewidth: 3, vertexColors: true, transparent: true, opacity: 0.95, worldUnits: false });
    const groundMat = new LineMaterial({ linewidth: 2, vertexColors: true, transparent: true, opacity: 0.9, worldUnits: false });
    const footMat = new LineMaterial({
      linewidth: 1.6, color: 0xffd866, dashed: true, dashSize: 0.025, gapSize: 0.018, transparent: true, opacity: 0.9, worldUnits: false,
    });
    const nadirMat = new LineMaterial({ linewidth: 1.2, color: 0xffffff, transparent: true, opacity: 0.55, worldUnits: false });
    const orbitLine = new Line2(new LineGeometry(), orbitMat);
    const groundLine = new Line2(new LineGeometry(), groundMat);
    const footLine = new Line2(new LineGeometry(), footMat);
    const nadir = new Line2(new LineGeometry(), nadirMat);
    for (const l of [orbitLine, groundLine, footLine, nadir]) l.visible = false;
    scene.add(groundLine, orbitLine, footLine, nadir);

    const satTex = loader.load(satelliteImg);
    satTex.colorSpace = THREE.SRGBColorSpace;
    const sat = new THREE.Sprite(new THREE.SpriteMaterial({ map: satTex }));
    sat.scale.set(0.2, 0.2 * SAT_ASPECT, 1);
    const haloTex = glowTexture("34,184,216");
    const halo = new THREE.Sprite(
      new THREE.SpriteMaterial({ map: haloTex, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true }),
    );
    halo.scale.set(0.42, 0.42, 1);
    const ring = new THREE.Mesh(
      new THREE.RingGeometry(0.018, 0.024, 48),
      new THREE.MeshBasicMaterial({ color: 0xffd866, transparent: true, opacity: 0.9, side: THREE.DoubleSide, depthWrite: false }),
    );
    const dot = new THREE.Mesh(new THREE.CircleGeometry(0.009, 24), new THREE.MeshBasicMaterial({ color: 0xffd866, side: THREE.DoubleSide }));
    const marks = [sat, halo, ring, dot];
    for (const o of marks) {
      o.visible = false;
      scene.add(o);
    }

    const cam = { dist: HOME_DIST, lat: 20, lon: 78 };
    const target = { lat: 20, lon: 78, dist: HOME_DIST };
    let followOn = follow;
    let cur = null;

    const setLine = (line, pts, colors) => {
      if (pts.length < 6) {
        line.visible = false;
        return;
      }
      const g = new LineGeometry();
      g.setPositions(pts);
      if (colors) g.setColors(colors);
      line.geometry.dispose();
      line.geometry = g;
      line.computeLineDistances();
      line.visible = true;
    };

    const setTrack = (t) => {
      const g = [];
      const o = [];
      for (const p of t) {
        g.push(...llaToXyz(p.lat, p.lon, 0, 1.004));
        o.push(...llaToXyz(p.lat, p.lon, p.altKm, 1));
      }
      setLine(orbitLine, o, fadeColors(t.length, [0.56, 0.89, 0.96], 0.08));
      setLine(groundLine, g, fadeColors(t.length, [0.13, 0.72, 0.85], 0.05));
    };
    const setCurrent = (c) => {
      cur = c;
      for (const o of marks) o.visible = Boolean(c);
      if (!c) return;
      const p = new THREE.Vector3(...llaToXyz(c.lat, c.lon, c.altKm, 1));
      const s = new THREE.Vector3(...llaToXyz(c.lat, c.lon, 0, 1.003));
      sat.position.copy(p);
      halo.position.copy(p);
      for (const o of [ring, dot]) {
        o.position.copy(s);
        o.lookAt(s.clone().multiplyScalar(2));
      }
      setLine(nadir, [s.x, s.y, s.z, p.x, p.y, p.z]);
      setLine(footLine, footprint(c.lat, c.lon, c.altKm, 3).flatMap(([la, lo]) => llaToXyz(la, lo, 0, 1.005)));
      if (followOn) {
        target.lat = Math.max(-55, Math.min(55, c.lat * 0.8));
        target.lon = c.lon;
      }
    };
    api.current = {
      setTrack,
      setCurrent,
      setFollow: (f) => {
        followOn = f;
        if (f && cur) setCurrent(cur);
      },
      reset: () => {
        target.dist = HOME_DIST;
        followOn = true;
        if (cur) setCurrent(cur);
      },
    };

    // Drag to turn, wheel to zoom. Turning by hand switches follow off.
    let drag = null;
    const down = (e) => {
      drag = { x: e.clientX, y: e.clientY };
      el.setPointerCapture(e.pointerId);
    };
    const move = (e) => {
      if (!drag) return;
      const k = 0.22 * (cam.dist / HOME_DIST);
      target.lon -= (e.clientX - drag.x) * k;
      target.lat = Math.max(-80, Math.min(80, target.lat + (e.clientY - drag.y) * k));
      drag = { x: e.clientX, y: e.clientY };
      if (followOn) {
        followOn = false;
        turnedByHand();
      }
    };
    const up = () => {
      drag = null;
    };
    const wheel = (e) => {
      e.preventDefault();
      target.dist = Math.max(1.45, Math.min(7, target.dist * (e.deltaY > 0 ? 1.08 : 0.93)));
    };
    el.addEventListener("pointerdown", down);
    el.addEventListener("pointermove", move);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    el.addEventListener("wheel", wheel, { passive: false });

    const size = () => {
      const w = Math.max(1, box.clientWidth);
      const h = Math.max(1, box.clientHeight);
      renderer.setSize(w, h, false);
      camera.aspect = w / h;
      const need = 2 * THREE.MathUtils.radToDeg(Math.atan(1.2 / (cam.dist * Math.max(camera.aspect, 0.05))));
      camera.fov = Math.min(Math.max(34, need), 80);
      camera.updateProjectionMatrix();
      for (const m of [orbitMat, groundMat, footMat, nadirMat]) m.resolution.set(w, h);
    };
    size();
    const ro = new ResizeObserver(size);
    ro.observe(box);

    let raf = 0;
    const t0 = performance.now();
    const frame = () => {
      const t = (performance.now() - t0) / 1000;
      const dLon = ((target.lon - cam.lon + 540) % 360) - 180;
      cam.lon += dLon * 0.05;
      cam.lat += (target.lat - cam.lat) * 0.05;
      cam.dist += (target.dist - cam.dist) * 0.08;
      const ss = subsolar(cur ? new Date(cur.t) : new Date());
      const sun = new THREE.Vector3(...llaToXyz(ss.lat, ss.lon, 0, 1)).normalize();
      earthMat.uniforms.sunDir.value.copy(sun);
      atmoMat.uniforms.sunDir.value.copy(sun);
      // gentle pulse on the halo and the ring under the spacecraft
      const pulse = reduceMotion ? 0.5 : (Math.sin(t * 2.4) + 1) / 2;
      halo.scale.setScalar(0.36 + 0.12 * pulse);
      halo.material.opacity = 0.55 + 0.35 * pulse;
      ring.scale.setScalar(reduceMotion ? 1.4 : 1 + ((t * 0.7) % 1) * 1.8);
      ring.material.opacity = reduceMotion ? 0.8 : 0.9 * (1 - ((t * 0.7) % 1));
      camera.position.set(...llaToXyz(cam.lat, cam.lon, 0, 1)).normalize().multiplyScalar(cam.dist);
      camera.lookAt(0, 0, 0);
      renderer.render(scene, camera);
      raf = requestAnimationFrame(frame);
    };
    frame();

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
      el.removeEventListener("pointerdown", down);
      el.removeEventListener("pointermove", move);
      el.removeEventListener("pointerup", up);
      el.removeEventListener("pointercancel", up);
      el.removeEventListener("wheel", wheel);
      scene.traverse((o) => {
        o.geometry?.dispose?.();
        if (Array.isArray(o.material)) o.material.forEach((m) => m.dispose());
        else o.material?.dispose?.();
      });
      for (const tex of [dayT, nightT, satTex, haloTex]) tex.dispose();
      renderer.dispose();
      api.current = null;
    };
    // built once; data arrives through the api below
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => api.current?.setTrack(track), [track]);
  useEffect(() => api.current?.setCurrent(current), [current]);
  useEffect(() => api.current?.setFollow(follow), [follow]);
  useEffect(() => {
    if (resetKey) api.current?.reset();
  }, [resetKey]);

  return (
    <div className="globe" ref={stage}>
      <canvas ref={canvas} aria-label="3D globe showing the spacecraft position, orbit trail and ground track" role="img" />
      {noGl && (
        <div className="globe-fallback">
          The 3D view needs WebGL, which this browser has turned off. The map below shows the same track.
        </div>
      )}
    </div>
  );
}
