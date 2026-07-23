"""Gymnasium launcher for the Wi-Fi 6 ns-3 environment."""

from pathlib import Path
import sys

import gymnasium as gym


class WifiNs3Env:
    """Expose the ns3-ai Gymnasium environment with Wi-Fi-specific defaults."""

    def __init__(self,
                 ns3_path: str | Path | None = None,
                 decision_interval: float = 0.1,
                 traffic_stop: float = 19.0):
        self.ns3_path = Path(ns3_path or Path(__file__).resolve().parents[3])
        ai_python = self.ns3_path / "contrib" / "ns3-ai" / "model" / "gym-interface" / "py"
        ai_utils = self.ns3_path / "contrib" / "ns3-ai" / "python_utils"
        for path in (ai_python, ai_utils):
            if str(path) not in sys.path:
                sys.path.insert(0, str(path))
        import ns3ai_gym_env  # noqa: F401 -- registers the Gym environment
        self._env = gym.make(
            "ns3ai_gym_env/Ns3-v0",
            targetName="wifi6_sim02",
            ns3Path=str(self.ns3_path),
            ns3Settings={
                "gym": True,
                "decisionInterval": decision_interval,
                "trafficStop": traffic_stop,
            },
        )
        self.observation_space = self._env.observation_space
        self.action_space = self._env.action_space

    def reset(self, *, seed=None, options=None):
        return self._env.reset(seed=seed)

    def step(self, action):
        return self._env.step(action)

    def close(self):
        self._env.close()
