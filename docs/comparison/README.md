# Timer Data Structures Comparison

This project implements and compares two timer data structures:
1. TimerWheel
2. Lawn Timer

## Overview

This project is designed for academic research comparing the performance characteristics of different timer data structures. It provides implementations of both TimerWheel and Lawn timer algorithms, along with benchmarking tools to evaluate their performance under various conditions.

## Project Structure

```
.
├── src/
│   ├── timer_wheel.py      # TimerWheel implementation
│   ├── lawn_timer.py       # Lawn Timer implementation
│   └── benchmarks.py       # Benchmarking utilities
├── tests/
│   ├── test_timer_wheel.py
│   ├── test_lawn_timer.py
│   └── test_benchmarks.py
├── requirements.txt
└── README.md
```

## Installation

1. Create a virtual environment:
```bash
python -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
```

2. Install dependencies:
```bash
pip install -r requirements.txt
```

## Usage

Run the benchmarks:
```bash
python src/benchmarks.py
```

Run tests:
```bash
pytest tests/
```

## Implementation Details

### TimerWheel
The TimerWheel implementation uses a circular buffer of buckets, where each bucket contains timers that expire at the same time. This provides O(1) insertion and deletion operations for timers.

### Lawn Timer
The Lawn Timer implementation uses a hierarchical structure that provides efficient timer management with O(1) complexity for most operations.

## Benchmarking

The project includes comprehensive benchmarking tools to compare:
- Insertion time
- Deletion time
- Memory usage
- Scalability with different timer counts
- Performance under various load conditions

## Contributing

Feel free to submit issues and enhancement requests! 