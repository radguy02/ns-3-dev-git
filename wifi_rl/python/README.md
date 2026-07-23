# Wi-Fi 6 Gymnasium wrapper

Build `wifi6_sim02`, then run the smoke test from this directory:

```sh
PYTHONPATH=. python agents/random_agent.py
```

`WifiNs3Env.reset()` creates a fresh ns-3 process and `step()` exchanges a
discrete action through ns3-ai shared memory. Actions are `0` (equal), `1`
(video), `2` (voice), and `3` (IoT). The C++ scheduler currently records the
selected allocation; mapping these weights to Wi-Fi resource units is the next
scheduler implementation task.
