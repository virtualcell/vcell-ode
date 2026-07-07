from pyvcell_odesolver._internal.native_calls import ODENativeCalls

def version() -> str:
    obj = ODENativeCalls()
    return obj.call_version()

def solve(input_file_path: str, output_file_path: str, task_id: int = -1) -> int:
    obj = ODENativeCalls()
    return obj.call_solve(input_file_path, output_file_path, task_id)

if __name__ == "__main__":
    print(version())