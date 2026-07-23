"""Run a short multi-episode ns3-ai/Gymnasium stability check."""

from env.wifi_env import WifiNs3Env


def main() -> None:
    env = WifiNs3Env(decision_interval=1.0, traffic_stop=3.0)
    try:
        for episode in range(3):
            _, _ = env.reset(seed=episode)
            terminated = truncated = False
            total_reward = 0.0
            steps = 0
            while not (terminated or truncated):
                _, reward, terminated, truncated, _ = env.step(env.action_space.sample())
                total_reward += reward
                steps += 1
            print(f"episode={episode} steps={steps} reward={total_reward:.3f}")
    finally:
        env.close()


if __name__ == "__main__":
    main()
