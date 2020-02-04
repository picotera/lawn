import argparse
import subprocess

def calc_all_permutations(options_dict, permutatable_field):
    permutation_params = options_dict[permutatable_field][1]
    basic_param_dict = {}
    for option in options_dict:
        param_options = options_dict[option][1]
        param_default = options_dict[option][0]
        basic_param_dict[option] = param_options[param_default]
    
    for perm_value in permutation_params:
        basic_param_dict[permutatable_field] = perm_value
        yield basic_param_dict



def results_to_dict(results):
    keys = []
    res_dict = {}
    found_title_line = False
    for i, line in enumerate(results):
        line_parts = [p.strip() for p in line.split("|")]
        if len(line_parts) > 2 and line_parts[0] == "T":
            if not found_title_line:
                keys = line_parts
                for key in keys:
                    res_dict[key] = []
                found_title_line = True
            else:
                for key, part in zip(keys, line_parts):
                    res_dict[key].append(part)
    return res_dict



# run test via calling the programm
def run_test(params, is_dry=True):
    command = ["./test_benchmark.run"]
    if is_dry:
        command.append("--dry-run")
    else:
        command.append("--script-mode")

    for param in params:
        command.append("--" + param)
        command.append(str(params[param]))
    print(command)
    result = subprocess.run(command, stdout=subprocess.PIPE)
    print(result.stdout.decode('utf-8'))
    # return
    # res = system.call("") #TODO:
    # return results_to_dict(res)


# for number of timers PRELOAD in the DS, measure:
# elapsed time for ITERATIONS insert/del 
# jitter/drift (mean,max diviation from expiration)
def plot_histogram_graph(test_results):
    all_histogram_results = extract_values("histogram")



    ...

    y,x = parse_histogram(histogram_results)
    #dont forget to subdevide to lawn and wheel
    pass


def plot_scatter_graph(y_axis_title, x_axis_title, test_results):
    t_results = test_results.get("T")
    y_results = test_results.get(y_axis_title)
    x_results = test_results.get(x_axis_title)
    lawn_result_idxs = [i for i,t in enumerate(t_results) if t=="L"]
    wheel_result_idxs = [i for i,t in enumerate(t_results) if t=="W"]

    lawn_y_values = [y_results[i] for i in lawn_result_idxs]
    lawn_x_values = [x_results[i] for i in lawn_result_idxs]

    wheel_y_values = [y_results[i] for i in wheel_result_idxs]
    wheel_x_values = [x_results[i] for i in wheel_result_idxs]

    # TODO: plot




################## MAIN ####################

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Benchmark toolkit for the Lawn DS')
    parser.add_argument('--repeat-tests', dest='repeat', type=int, default=10, 
        help='The number of times to repeat every test')
    parser.add_argument('--run-tests', dest='run_tests', action='store_true', default=False,
        help='run tests and save results')
    parser.add_argument('--analize', dest='analize', action='store_false', default=True,
        help='analize saved results')
    parser.add_argument('--dry-run', dest='dry_run', action='store_true', default=False,
        help='just print the actions that would have taken place')


    args = parser.parse_args()
    options_dict = {
    #    param name ---default idx ------------ value options
        "unique-ttls" :    (5,     [1, 5, 100, 500, 1000, 2000, 5000, 10000]),
        "preload-size" :   (-1,    [0, 1000, 1000000, 10000000, 50000000]),
        "inserts" :        (-1,    [0, 1000, 10000, 1000000]),
        "deletions" :      (-1,    [0, 1000, 10000, 1000000]),
        "expirations" :    (0,     [0, 1000, 10000, 1000000]),
        "indel-actions" :  (0,     [0, 100, 1000, 10000, 100000, 1000000, 5000000, 50000000]),
        "repeat" :         (0,     [args.repeat]),
        "histogram-size" : (0,     [20]),
    }


    print("\n======= UNIQUE TTLs ========")
    #["indel-actions", "unique_ttls", "preload-size", "expirations"]
    test_results = []
    if args.run_tests:
        for params in calc_all_permutations(options_dict, "unique-ttls"):
            run_test(params, args.dry_run)
            # test_result = run_test(params)

    if args.analize:
        # plot_scatter_graph("jitter avg (ms)", "TTLs", test_result)
        pass
        # plot_histogram_graph(test_result)


    print("\n======= PRELOAD SIZE ========")

    if args.run_tests:
        for params in calc_all_permutations(options_dict, "preload-size"):
            run_test(params, args.dry_run)
            # test_result = run_test(params)

    if args.analize:
        plot_scatter_graph("jitter avg (ms)", "preload", test_result)
        plot_histogram_graph(test_result)

    print("\n======= IN/DEL ACTIONS ========")

    if args.run_tests:
        for params in calc_all_permutations(options_dict, "indel-actions"):
            run_test(params, args.dry_run)
            # test_result = run_test(params)

    if args.analize:
        plot_scatter_graph("insert total (ms)", "indel-actions", test_result)
        plot_scatter_graph("delete total (ms)", "indel-actions", test_result)
        
        
        # test_results.append(test_result)
        # TODO: can we learn sotmthing from combinig graphs?
