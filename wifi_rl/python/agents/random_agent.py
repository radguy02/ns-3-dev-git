"""Smoke-test agent for the Wi-Fi ns-3 Gymnasium environment."""

from env.wifi_env import WifiNs3Env


def main(episodes: int = 3) -> None:
    env = WifiNs3Env(decision_interval=1.0)
    try:
        for episode in range(episodes):
            observation, info = env.reset(seed=episode)
            terminated = truncated = False
            total_reward = 0.0
            while not (terminated or truncated):
                observation, reward, terminated, truncated, info = env.step(env.action_space.sample())
                total_reward += reward
            print(f"episode={episode} reward={total_reward:.3f} info={info}")
    finally:
        env.close()


if __name__ == "__main__":
    main()
