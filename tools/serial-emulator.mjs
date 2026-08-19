#!/usr/bin/env node
/**
 * Deterministic development emulator for drone-telemetry-serial/v1.
 * It emits the same NDJSON messages produced by main/serial_bridge.c and
 * accepts the same commands on stdin. It never emulates or accesses RF.
 *
 * Usage:
 *   node tools/serial-emulator.mjs --once
 *   node tools/serial-emulator.mjs
 */
import readline from "node:readline";

const once = process.argv.includes("--once");
const scenarioNames = ["field-demo", "sparse", "dense"];
const baseAircraft = [
  { id: "BRA-UAS-001", protocol: 6, altitude: 125, speed: 14.5, rssi: -64, distance: 420, azimuth: 30 },
  { id: "ELRS-RACER-9", protocol: 0, altitude: 78, speed: 24, rssi: -75, distance: 650, azimuth: 190 },
  { id: "DJI-MAVIC-4K", protocol: 1, altitude: 55, speed: 7.2, rssi: -58, distance: 280, azimuth: 310 },
];

let state = { simulation: false, scenario: 0, tick: 0, shortcuts: [] };
const emit = (payload) => process.stdout.write(`${JSON.stringify(payload)}\n`);
const normalizedScenario = () => Math.max(0, Math.min(2, state.scenario));

function aircraftForTick() {
  const profile = normalizedScenario();
  return baseAircraft.map((aircraft, index) => {
    const density = profile === 2 ? 0.55 : profile === 1 && index > 0 ? 4.25 : 1;
    const movement = state.tick * (profile === 2 ? 3.2 : 1.25) + index * 19;
    const radians = (aircraft.azimuth + movement) * (Math.PI / 180);
    const distance = aircraft.distance * density;
    return {
      type: "aircraft", id: aircraft.id, protocol: aircraft.protocol,
      lat: -23.55052 + (Math.cos(radians) * distance) / 111320,
      lon: -46.633309 + (Math.sin(radians) * distance) / 102000,
      altitude: +(aircraft.altitude + Math.sin(state.tick / 3 + index) * 5).toFixed(1),
      speed: aircraft.speed, rssi: aircraft.rssi + Math.round(Math.sin(state.tick / 2 + index) * 4),
      distance: +distance.toFixed(1), azimuth: +((aircraft.azimuth + movement) % 360).toFixed(1),
    };
  });
}

function publishSnapshot() {
  const aircraft = state.simulation ? aircraftForTick() : [];
  emit({ type: "state", active: aircraft.length, wifi: true, ble: true, sx1262: true,
    gps: true, sd: true, simulation: state.simulation, scenario: normalizedScenario(),
    scenarioName: scenarioNames[normalizedScenario()], tick: state.tick });
  aircraft.forEach(emit);
  if (state.simulation) state.tick += 1;
}

function handleCommand(line) {
  let command;
  try { command = JSON.parse(line); } catch { emit({ type: "ack", ok: false, error: "invalid-json" }); return; }
  if (command.action === "simulation") {
    state.simulation = Boolean(command.enabled);
    if (Number.isInteger(command.scenario)) state.scenario = command.scenario;
    state.tick = 0;
  } else if (command.action === "shortcut" && Number.isInteger(command.key) && command.key >= 1 && command.key <= 7) {
    state.shortcuts.push(command.key);
  } else if (command.action !== "sync") {
    emit({ type: "ack", ok: false, error: "unknown-command" }); return;
  }
  emit({ type: "ack", ok: true });
  publishSnapshot();
}

emit({ type: "hello", protocol: "drone-telemetry-serial/v1", transport: "emulator" });
publishSnapshot();
if (!once) {
  readline.createInterface({ input: process.stdin, crlfDelay: Infinity }).on("line", handleCommand);
  setInterval(publishSnapshot, 1000).unref();
}
