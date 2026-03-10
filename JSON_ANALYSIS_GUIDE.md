# JSON 로그 분석 가이드

이 문서는 JNI 하네스가 생성한 JSON 로그를 분석하는 다양한 방법을 제공합니다.

## 🔍 JSON 로그 구조

```json
{
  "log_version": "1.0",
  "timestamp": 1740556245,
  "calls": [
    {
      "index": 0,
      "timestamp_sec": 1234,
      "timestamp_nsec": 567890000,
      "function": "FindClass",
      "caller_address": "0x784f4c0cf8",
      "caller_offset": "0x10cf8",
      "caller_module": "libengine.so",
      "arguments": {
        "env": "0x7ffc12345678",
        "name": "com/example/MyClass",
        "jclass": "0x12000"
      }
    },
    {
      "index": 1,
      "function": "GetMethodID",
      "caller_address": "0x784f4c0d24",
      "caller_offset": "0x10d24",
      "caller_module": "libengine.so",
      "arguments": {
        "clazz": "0x12000",
        "class_name": "com/example/MyClass",
        "name": "myMethod",
        "sig": "(I)V"
      }
    }
  ],
  "total_calls": 2
}
```

### 필드 설명

- `log_version`: 로그 포맷 버전
- `timestamp`: 로그 시작 Unix 타임스탬프
- `calls[]`: JNI 함수 호출 배열
  - `index`: 호출 순서 (0부터 시작)
  - `timestamp_sec`: 호출 시각 (초)
  - `timestamp_nsec`: 호출 시각 (나노초)
  - `function`: JNI 함수 이름
  - **`caller_address`**: 호출한 코드의 런타임 메모리 주소
  - **`caller_offset`**: 호출한 코드의 .so 내 offset (IDA/Ghidra용)
  - **`caller_module`**: 호출한 .so 파일명
  - `arguments`: 함수 인자들 (key-value)
    - **`class_name`**: jclass에 대응하는 클래스 이름 (해당 함수만)
- `total_calls`: 전체 호출 횟수

---

## 🎯 역공학 분석 (IDA/Ghidra)

### caller_offset으로 코드 위치 찾기

JSON에서 offset을 확인하여 IDA/Ghidra에서 직접 점프:

```bash
# 특정 함수의 호출 위치 확인
jq '.calls[] | select(.function == "RegisterNatives") | 
    {function, caller_offset, caller_module}' logs/jni_hook.json
```

출력 예:
```json
{
  "function": "RegisterNatives",
  "caller_offset": "0x10cf8",
  "caller_module": "libengine.so"
}
```

**IDA 사용법:**
1. `libengine.so` 열기
2. `G` 키 누르기
3. `0x10cf8` 입력
4. 해당 코드로 바로 점프! 🎯

### 호출 지점 맵 생성

```python
import json

with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

# 모듈별 호출 지점 추출
call_sites = {}
for call in log_data['calls']:
    module = call.get('caller_module', 'unknown')
    offset = call.get('caller_offset', 'null')
    func = call['function']
    
    if module not in call_sites:
        call_sites[module] = []
    
    call_sites[module].append({
        'offset': offset,
        'function': func,
        'index': call['index']
    })

# IDA 스크립트용 출력
for module, sites in call_sites.items():
    print(f"\n# {module}")
    for site in sites:
        print(f"# [{site['index']}] {site['offset']}: {site['function']}")
```

---

## 📊 jq를 이용한 분석

### 설치

```bash
# Linux
sudo apt-get install jq

# Windows
choco install jq
```

### 기본 사용법

#### 1. 전체 로그 Pretty Print

```bash
cat logs/jni_hook.json | jq .
```

#### 2. 특정 함수만 필터링

```bash
# FindClass 호출만 추출
jq '.calls[] | select(.function == "FindClass")' logs/jni_hook.json

# GetMethodID 호출만 추출
jq '.calls[] | select(.function == "GetMethodID")' logs/jni_hook.json

# 특정 클래스 이름으로 필터링 (class_name 필드 이용)
jq '.calls[] | select(.arguments.class_name == "com/example/MainActivity")' logs/jni_hook.json
```

#### 3. 함수별 호출 횟수

```bash
jq '[.calls[].function] | group_by(.) | map({function: .[0], count: length})' logs/jni_hook.json
```

출력 예:
```json
[
  {"function": "FindClass", "count": 15},
  {"function": "GetMethodID", "count": 23},
  {"function": "RegisterNatives", "count": 2}
]
```

#### 4. 특정 클래스 검색

```bash
# "MainActivity"를 포함하는 FindClass 호출
jq '.calls[] | select(.function == "FindClass" and (.arguments.name | contains("MainActivity")))' logs/jni_hook.json
```

#### 5. 호출 위치 분석 (IDA/Ghidra용)

```bash
# caller_offset이 있는 모든 호출 (역공학 분석용)
jq '.calls[] | select(.caller_offset != null) | 
    {index, function, caller_offset, caller_module}' logs/jni_hook.json

# 특정 모듈에서 발생한 호출만 필터링
jq '.calls[] | select(.caller_module == "libengine.so")' logs/jni_hook.json

# 모듈별 호출 횟수
jq '[.calls[].caller_module] | group_by(.) | map({module: .[0], count: length})' logs/jni_hook.json
```

#### 6. 시간순 상위 N개 호출

```bash
# 최근 10개 호출
jq '.calls[-10:]' logs/jni_hook.json

# 최초 10개 호출
jq '.calls[:10]' logs/jni_hook.json
```

#### 7. 함수별 인자 통계

```bash
# FindClass로 검색된 모든 클래스 이름
jq '.calls[] | select(.function == "FindClass") | .arguments.name' logs/jni_hook.json | sort -u

# class_name이 기록된 모든 JNI 호출
jq '.calls[] | select(.arguments.class_name != null) | 
    {function, class_name: .arguments.class_name, name: .arguments.name}' logs/jni_hook.json
```

#### 8. RegisterNatives 상세 정보

```bash
# 등록된 모든 네이티브 메서드
jq '.calls[] | select(.function == "RegisterNatives") | .arguments.methods_detail' logs/jni_hook.json
```

---

## 🐍 Python을 이용한 분석

### 기본 로드 및 분석

```python
import json
from collections import Counter

# JSON 로그 로드
with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

# 함수별 호출 횟수
function_counts = Counter(call['function'] for call in log_data['calls'])
print("함수별 호출 횟수:")
for func, count in function_counts.most_common():
    print(f"  {func}: {count}")

# FindClass로 검색된 클래스들
classes = [call['arguments']['name'] 
           for call in log_data['calls'] 
           if call['function'] == 'FindClass']
print(f"\n검색된 클래스 ({len(classes)}개):")
for cls in sorted(set(classes)):
    print(f"  {cls}")

# GetMethodID로 검색된 메서드들
methods = [(call['arguments']['name'], call['arguments']['sig'])
           for call in log_data['calls']
           if call['function'] == 'GetMethodID']
print(f"\n검색된 메서드 ({len(methods)}개):")
for name, sig in sorted(set(methods)):
    print(f"  {name} {sig}")

# 호출 위치 분석 (IDA/Ghidra용)
print("\n모듈별 호출 지점:")
from collections import defaultdict
module_calls = defaultdict(list)

for call in log_data['calls']:
    module = call.get('caller_module')
    offset = call.get('caller_offset')
    if module and offset:
        module_calls[module].append((offset, call['function'], call['index']))

for module, calls in module_calls.items():
    print(f"\n{module}:")
    for offset, func, idx in calls[:5]:  # 처음 5개만
        print(f"  [{idx}] {offset}: {func}")
```

### 시간 분석

```python
import json
import matplotlib.pyplot as plt

with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

# 함수별 호출 시간 분포
from collections import defaultdict
call_times = defaultdict(list)

for call in log_data['calls']:
    time_ms = call['timestamp_sec'] * 1000 + call['timestamp_nsec'] / 1_000_000
    call_times[call['function']].append(time_ms)

# 그래프 그리기
fig, ax = plt.subplots(figsize=(12, 6))
for func, times in call_times.items():
    ax.scatter(times, [func] * len(times), alpha=0.6, label=func)

ax.set_xlabel('Time (ms)')
ax.set_ylabel('Function')
ax.set_title('JNI Function Call Timeline')
plt.tight_layout()
plt.savefig('jni_timeline.png')
print("Timeline saved to jni_timeline.png")
```

### 패턴 분석

```python
import json
from typing import Counter
with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

# 연속 호출 패턴 분석 (n-gram)
def find_call_patterns(calls, n=3):
    patterns = []
    for i in range(len(calls) - n + 1):
        pattern = tuple(calls[i+j]['function'] for j in range(n))
        patterns.append(pattern)
    return Counter(patterns)

patterns = find_call_patterns(log_data['calls'], n=3)
print("가장 흔한 호출 패턴 (3-gram):")
for pattern, count in patterns.most_common(10):
    print(f"  {' -> '.join(pattern)}: {count}회")

# FindClass와 GetMethodID 쌍 찾기
pairs = []
for i, call in enumerate(log_data['calls'][:-1]):
    if call['function'] == 'FindClass':
        next_call = log_data['calls'][i+1]
        if next_call['function'] == 'GetMethodID':
            pairs.append({
                'class': call['arguments']['name'],
                'method': next_call['arguments']['name'],
                'signature': next_call['arguments']['sig']
            })

print(f"\nFindClass -> GetMethodID 쌍 ({len(pairs)}개):")
for pair in pairs[:10]:
    print(f"  {pair['class']}.{pair['method']} {pair['signature']}")
```

---

## 📈 데이터베이스 임포트

### SQLite

```python
import json
import sqlite3

# 데이터베이스 생성
conn = sqlite3.connect('jni_analysis.db')
cursor = conn.cursor()

# 테이블 생성
cursor.execute('''
CREATE TABLE IF NOT EXISTS jni_calls (
    id INTEGER PRIMARY KEY,
    call_index INTEGER,
    timestamp_sec INTEGER,
    timestamp_nsec INTEGER,
    function TEXT,
    arguments TEXT
)
''')

# JSON 데이터 로드 및 삽입
with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

for call in log_data['calls']:
    cursor.execute('''
        INSERT INTO jni_calls (call_index, timestamp_sec, timestamp_nsec, function, arguments)
        VALUES (?, ?, ?, ?, ?)
    ''', (
        call['index'],
        call['timestamp_sec'],
        call['timestamp_nsec'],
        call['function'],
        json.dumps(call['arguments'])
    ))

conn.commit()
print(f"Imported {len(log_data['calls'])} calls to database")

# 쿼리 예제
cursor.execute('''
    SELECT function, COUNT(*) as count 
    FROM jni_calls 
    GROUP BY function 
    ORDER BY count DESC
''')
print("\n함수별 호출 횟수:")
for row in cursor.fetchall():
    print(f"  {row[0]}: {row[1]}")

conn.close()
```

---

## 🛠️ 유용한 스크립트 (고도화 필요 항목)

### ~~1. 클래스 의존성 그래프 생성~~

```python
import json
import networkx as nx
import matplotlib.pyplot as plt

with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

# 그래프 생성
G = nx.DiGraph()

# FindClass 호출로부터 노드 추가
for call in log_data['calls']:
    if call['function'] == 'FindClass':
        class_name = call['arguments']['name']
        G.add_node(class_name)

# 연속적인 클래스 접근을 엣지로 추가
classes = [c['arguments']['name'] for c in log_data['calls'] if c['function'] == 'FindClass']
for i in range(len(classes) - 1):
    G.add_edge(classes[i], classes[i+1])

# 시각화
plt.figure(figsize=(12, 8))
pos = nx.spring_layout(G)
nx.draw(G, pos, with_labels=True, node_color='lightblue', 
        node_size=1500, font_size=8, arrows=True)
plt.title('Class Access Dependency Graph')
plt.savefig('class_dependency.png')
print("Dependency graph saved to class_dependency.png")
```

### ~~2. JNI 호출 리포트 생성~~

```python
import json
from datetime import datetime

with open('logs/jni_hook.json', 'r') as f:
    log_data = json.load(f)

# HTML 리포트 생성
html = f"""
<html>
<head>
    <title>JNI Analysis Report</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; }}
        table {{ border-collapse: collapse; width: 100%; }}
        th, td {{ border: 1px solid #ddd; padding: 8px; text-align: left; }}
        th {{ background-color: #4CAF50; color: white; }}
    </style>
</head>
<body>
    <h1>JNI Function Call Analysis Report</h1>
    <p>Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
    <p>Total Calls: {log_data['total_calls']}</p>
    
    <h2>Function Call Summary</h2>
    <table>
        <tr><th>Function</th><th>Count</th><th>Percentage</th></tr>
"""

from collections import Counter
function_counts = Counter(call['function'] for call in log_data['calls'])
for func, count in function_counts.most_common():
    percentage = (count / log_data['total_calls']) * 100
    html += f"<tr><td>{func}</td><td>{count}</td><td>{percentage:.2f}%</td></tr>\n"

html += """
    </table>
    
    <h2>Recent Calls</h2>
    <table>
        <tr><th>Index</th><th>Function</th><th>Arguments</th></tr>
"""

for call in log_data['calls'][-20:]:  # 최근 20개
    args_str = ', '.join(f"{k}={v}" for k, v in call['arguments'].items())
    html += f"<tr><td>{call['index']}</td><td>{call['function']}</td><td>{args_str}</td></tr>\n"

html += """
    </table>
</body>
</html>
"""

with open('jni_report.html', 'w') as f:
    f.write(html)

print("Report saved to jni_report.html")
```

---

## 💡 분석 팁

1. **성능 병목 찾기**: `timestamp_sec`와 `timestamp_nsec`를 사용해 함수 간 실행 시간 분석
2. **의심스러운 패턴**: 비정상적으로 많은 동일 함수 호출 찾기
3. **리플렉션 사용 추적**: `FindClass` + `GetMethodID` + `Call*Method` 패턴
4. **네이티브 등록 확인**: `RegisterNatives`로 등록된 함수들의 주소 분석
5. **문자열 처리**: `NewStringUTF`, `GetStringUTFChars` 사용 패턴
6. **역공학 분석**:
   - `caller_offset`으로 IDA/Ghidra에서 정확한 호출 위치 찾기
   - `caller_module`로 어떤 .so에서 JNI를 사용하는지 파악
   - `class_name` 필드로 jclass를 추적 (GetMethodID, GetFieldID 등)
7. **모듈 의존성**: 여러 .so가 JNI를 호출하는 경우 `caller_module` 분석


---

## 📚 추가 리소스

- [jq Manual](https://stedolan.github.io/jq/manual/)
- [Python json module](https://docs.python.org/3/library/json.html)
- [Elasticsearch Python Client](https://elasticsearch-py.readthedocs.io/)
- [NetworkX Documentation](https://networkx.org/documentation/stable/)
