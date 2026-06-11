import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const stage = document.getElementById('stage');
const hud = document.getElementById('hud');
const errBox = document.getElementById('err');

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
stage.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x14121a);
const camera = new THREE.PerspectiveCamera(50, 1, 0.1, 100000);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;

scene.add(new THREE.AmbientLight(0xffffff, 1.6));
const key = new THREE.DirectionalLight(0xfff2dd, 1.6);
key.position.set(1, 2, 1.5);
scene.add(key);
const rim = new THREE.DirectionalLight(0x8899ff, 0.7);
rim.position.set(-1.5, 0.5, -1);
scene.add(rim);

const grid = new THREE.GridHelper(1000, 20, 0x4a4168, 0x2a2640);
scene.add(grid);

let current = null;
const loader = new GLTFLoader();

function resize() {
  const w = stage.clientWidth, h = stage.clientHeight;
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', resize);
resize();

function frameObject(obj) {
  const box = new THREE.Box3().setFromObject(obj);
  const size = box.getSize(new THREE.Vector3());
  const center = box.getCenter(new THREE.Vector3());
  const radius = Math.max(size.x, size.y, size.z) * 0.6 || 100;
  camera.near = radius / 100;
  camera.far = radius * 50;
  camera.position.set(center.x + radius * 1.6, center.y + radius * 0.9, center.z + radius * 1.6);
  camera.updateProjectionMatrix();
  controls.target.copy(center);
  controls.update();
  grid.position.y = box.min.y;
  grid.scale.setScalar(radius / 250 || 1);
}

function show(url, title) {
  errBox.style.display = 'none';
  if (current) { scene.remove(current); current = null; }
  hud.textContent = `loading ${url}…`;
  loader.load(url, (gltf) => {
    current = gltf.scene;
    scene.add(current);
    frameObject(current);
    let tris = 0;
    current.traverse((n) => {
      if (n.isMesh && n.geometry.index) tris += n.geometry.index.count / 3;
    });
    hud.textContent = `${title} — ${tris.toLocaleString()} triangles — drag to orbit, scroll to zoom`;
  }, undefined, (e) => {
    errBox.style.display = 'grid';
    errBox.textContent = `failed to load ${url}: ${e.message || e}`;
    hud.textContent = '';
  });
}

function tick() {
  requestAnimationFrame(tick);
  controls.update();
  renderer.render(scene, camera);
}
tick();

const list = document.getElementById('list');
const resp = await fetch('models.json');
const entries = await resp.json();

entries.forEach((entry, i) => {
  const card = document.createElement('div');
  card.className = 'card';
  card.innerHTML = `<h2>${entry.title}</h2>
    <div class="src">${entry.source} · ${entry.arc}.arc</div>
    <div class="note">${entry.note}</div>`;
  const variants = document.createElement('div');
  variants.className = 'variants';
  entry.models.forEach((url, vi) => {
    const b = document.createElement('button');
    b.textContent = entry.models.length > 1 ? url.split('_').pop().replace('.gltf', '') : 'view';
    b.onclick = (ev) => {
      ev.stopPropagation();
      select(card, b, url, entry.title);
    };
    variants.appendChild(b);
  });
  card.appendChild(variants);
  card.onclick = () => select(card, variants.querySelector('button'), entry.models[0], entry.title);
  list.appendChild(card);
  if (i === 0) select(card, variants.querySelector('button'), entry.models[0], entry.title);
});

function select(card, btn, url, title) {
  document.querySelectorAll('.card').forEach((c) => c.classList.remove('active'));
  document.querySelectorAll('.variants button').forEach((b) => b.classList.remove('sel'));
  card.classList.add('active');
  if (btn) btn.classList.add('sel');
  show(url, title);
}
