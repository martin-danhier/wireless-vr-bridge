"""Utils for the for the handling of WVB measurement files."""

from matplotlib import image
import pandas as pd
import os
import numpy as np
import matplotlib.pyplot as plt
import skimage

EXPECTING_TABLE_NAME = 0
EXPECTING_COLUMN_NAMES = 1
EXPECTING_VALUES = 2


def wvb_combine_frame_times(measurement):
    """Combine the separate frame time tables into a single data frame."""

    driver_frame_time = measurement["driver_frame_time_measurements"]
    server_frame_time = measurement["server_frame_time_measurements"]
    client_frame_time = measurement["client_frame_time_measurements"]

    # All these tables have the "frame_id" column in common
    # Use that to merge them into a single table
    df = driver_frame_time.merge(server_frame_time, on="frame_id")
    df = df.merge(client_frame_time, on="frame_id", how="outer")

    # Reorder columns
    df = df[[
        "frame_id",
        "frame_index",
        "dropped",
        "frame_delay",
        # Driver times
        "present_called",
        "vsync",
        "frame_sent",
        "wait_for_present_called",
        "server_finished",
        "pose_updated_event",
        # Server times
        "frame_event_received",
        "present_info_received",
        "shared_texture_opened",
        "shared_texture_acquired",
        "staging_texture_mapped",
        "encoder_frame_pushed",
        "encoder_frame_pulled",
        "before_last_get_next_packet",
        "after_last_get_next_packet",
        "before_last_send_packet",
        "after_last_send_packet",
        "finished_signal_sent",
        # Client times
        "tracking_sampled",
        "last_packet_received",
        "pushed_to_decoder",
        "begin_wait_frame",
        "begin_frame",
        "after_wait_swapchain",
        "after_render",
        "end_frame",
        "predicted_present_time",
        "pose_timestamp"
    ]]

    # Replace nan with 0
    df = df.fillna(0)

    # Convert all columns to int
    df = df.astype('int64')

    return df


def wvb_combine_tracking_times(measurement, client_tracking=False):
    """Combine the separate tracking time tables into a single data frame."""

    if client_tracking:
        client_tracking_time = measurement["client_tracking_measurements"]
        client_tracking_time = client_tracking_time.rename(columns={
            "tracking_received": "client_tracking_received",
            "tracking_processed": "client_tracking_processed",
        })

    server_tracking_time = measurement["server_tracking_measurements"]
    server_tracking_time = server_tracking_time.rename(columns={
        "tracking_received": "server_tracking_received",
        "tracking_processed": "server_tracking_processed",
    })

    driver_tracking_time = measurement["driver_tracking_measurements"]
    driver_tracking_time = driver_tracking_time.rename(columns={
        "tracking_received": "driver_tracking_received",
        "tracking_processed": "driver_tracking_processed",
    })

    driver_access_time = measurement["driver_pose_access_measurements"]
    driver_access_time = driver_access_time.rename(columns={
        "pose_accessed": "driver_pose_accessed",
    })

    # All these tables have the "pose_timestamp" column in common
    # Use that to merge them into a single table
    if client_tracking:
        df = client_tracking_time.merge(
            server_tracking_time, on="pose_timestamp")
        df = df.merge(driver_tracking_time, on="pose_timestamp")
    else:
        df = server_tracking_time.merge(
            driver_tracking_time, on="pose_timestamp")
    df = df.merge(driver_access_time, on="pose_timestamp")

    # Convert all columns to int
    df = df.astype('int64')

    return df


def wvb_combine_all_times(measurement, client_tracking=False, keep_dropped=False):
    """Combine all time tables into a single data frame."""

    df = wvb_combine_tracking_times(measurement, client_tracking)
    df2 = wvb_combine_frame_times(measurement)
    df = df.merge(df2, on=["pose_timestamp"], how="outer")

    # Remove lines where driver_pose_accessed > present_called
    df = df[df["driver_pose_accessed"] < df["present_called"]]

    # For a given frame_id, there can be different driver_pose_accessed
    # Only keep the one that is maximal, but < than present_called
    if keep_dropped:
        # set frame_index == 0 to null
        df.loc[df["frame_index"] == 0, "frame_index"] = np.nan


        df = df.sort_values(by=["frame_id", "driver_pose_accessed"])
        df = df[df["frame_index"].duplicated() | df["frame_index"].isnull()]
    else:
        df = df.sort_values(by=["frame_id", "driver_pose_accessed"])
        df = df.drop_duplicates(subset=["frame_id"], keep="last")

    # Reorder columns: put id first then the rest
    l = [
        "frame_id",
        "frame_index",
        "dropped",
        "frame_delay",
        "pose_timestamp",
    ]
    df = df[l + [c for c in df.columns if c not in l]]

    return df


def wvb_load_measurements(path):
    """Loads a WVB measurement file into a pandas DataFrame."""

    # A wvb file is a concatenation of csv's in the format

    # table_name
    # column1,column2,column3,...
    # value1,value2,value3,...
    # value1,value2,value3,...
    # ...
    # ---

    measurements = {}

    state = EXPECTING_TABLE_NAME
    table_name = None
    n_cols = None

    with open(path, "r") as f:
        for line in f:
            line = line.strip()

            if state == EXPECTING_TABLE_NAME:
                if line == "---":
                    continue

                # Line should be an identifier
                if not line.isidentifier():
                    raise ValueError(f"Invalid table name: {line}")

                table_name = line

                state = EXPECTING_COLUMN_NAMES
            elif state == EXPECTING_COLUMN_NAMES:
                columns = line.split(",")
                n_cols = len(columns)

                for column in columns:
                    if not column.isidentifier():
                        raise ValueError(f"Invalid column name: {column}")

                measurements[table_name] = pd.DataFrame(columns=columns)

                state = EXPECTING_VALUES
            elif state == EXPECTING_VALUES:
                if line == "---":
                    state = EXPECTING_TABLE_NAME
                    continue

                values = line.split(",")

                # Only keep the first n_cols values
                values = values[:n_cols]

                if len(values) != n_cols:
                    raise ValueError(
                        f"Invalid number of values: {len(values)}")

                # For first line, detect types
                if measurements[table_name].empty:
                    measurements[table_name] = pd.DataFrame(
                        columns=columns, data=[values])
                else:
                    measurements[table_name].loc[len(
                        measurements[table_name])] = values

    return measurements


def wvb_load_measurement_pass(directory: str, pass_id: int):
    """Loads a measurement pass from a directory."""

    # List all wvb_measurements_pass_<pass_id>_<run_id>.csv files
    files = os.listdir(directory)
    files = [f for f in files if f.startswith(
        f"wvb_measurements_pass_{pass_id}_")]
    files = [f for f in files if f.endswith(".csv")]

    print(f"Found {len(files)} files for pass {pass_id}")

    measurements = []
    for f in files:
        measurements.append(wvb_load_measurements(os.path.join(directory, f)))

    return measurements


def wvb_average_table(measurements, table_name):
    """Averages a table from a measurement list."""

    if len(measurements) == 0:
        raise ValueError("No measurements to average")

    # Combine all measurements into a single dataframe
    # For numerical values, average them
    # For string values, only keep the first one
    df = None
    for m in measurements:
        if table_name not in m:
            raise ValueError(f"Table {table_name} not found in measurements")

        if df is None:
            df = m[table_name]
        else:
            df = pd.concat([df, m[table_name]])

    # Average the values
    df = df.groupby(df.index).mean()

    return df


def wvb_convert_time_to_delay(df):
    """Converts absolute timestamps to delays relative to the present_called column."""

    excluded = ["frame_id", "dropped", "frame_index",
                "frame_delay", "present_called"]

    df.loc[:, ~df.columns.isin(excluded)] = df.loc[:, ~df.columns.isin(
        excluded)].sub(df["present_called"], axis=0)

    # Subtract present_called by the first value
    if len(df) > 0:
        first = df["present_called"].iloc[0]
        df["present_called"] = df["present_called"] - first

    return df

def wvb_get_min_client_clock_error(df):
    # Client can have an error due to sync
    # But we know that tracking_sampled can NEVER occur after server_tracking_received.
    # So, we can compute the minimum error of each frame, take the max error and move client measurements by that amount across all frames.

    # Compute errors
    errs = df["server_tracking_received"] - df["tracking_sampled"]
    # A negative value means that server_tracking_received is before tracking_sampled, which is an error
    min_err = errs.min()
    if min_err >= 0:
        # No error
        return 0

    min_err = min_err - 500 # Add small delay for client->server transfer, should be much higher

    return -min_err

def wvb_get_client_clock_error_fn(data, only_take_min_of_run=True):
    """Fit a line to the clock error of the client."""

    # Find clock error
    errors = pd.DataFrame(columns=['time', 'error'])
    for pass_id in range(len(data)):
        for run_id in range(len(data[pass_id])):
            # Combine all tables
            df = wvb_combine_all_times(data[pass_id][run_id])
            # Remove dropped frames that don't have a tracking_sampled
            df = df[df['tracking_sampled'] != 0]

            # Create dataframe with ['present_called' -> time, 'server_tracking_received' - 'tracking_sampled' -> error]
            dt = pd.DataFrame(columns=['time', 'error'])
            dt['time'] = df['present_called']
            dt['error'] = df['server_tracking_received'] - df['tracking_sampled']

            if only_take_min_of_run:
                # Add the row with min error
                minimum = dt.loc[dt['error'].idxmin()]
                errors.loc[len(errors)] = minimum
            else:
                # Append all rows
                errors = pd.concat([errors, dt])

    # Remove duplicate times
    errors = errors.drop_duplicates(subset=['time'])
    errors = errors.sort_values(by=['time'])
    errors = errors.astype({'time': 'float64', 'error': 'float64'})

    # Remove outliers
    # errors = errors[(errors['error'] > errors['error'].quantile(0.05)) & (errors['error'] < errors['error'].quantile(0.95))]    

    m, c = np.polyfit(errors['time'], -errors['error'], 1)

    return m, c

def wvb_fix_client_clock_error(df, m=None, c=None):
    """Fix the client clock error by moving all client measurements by the error value,
    so that all tracking_sampled are before server_tracking_received."""

    if m is None or c is None:
        # Compute error based on the max error of the run
        err = wvb_get_min_client_clock_error(df)
        df["client_clock_error"] = err
    else:
        # Use function computed across all runs for more precise error
        df["client_clock_error"] = m * df["tracking_sampled"] + c

    # Move client measurements by min_err so that all tracking_sampled are before server_tracking_received
    client_cols = [
        "client_tracking_received",
        "client_tracking_processed",
        "tracking_sampled",
        "last_packet_received",
        "pushed_to_decoder",
        "begin_wait_frame",
        "begin_frame",
        "after_wait_swapchain",
        "after_render",
        "end_frame",
        "predicted_present_time",
        "pose_timestamp"
    ]
    client_cols = [c for c in client_cols if c in df.columns] # Filter out columns that don't exist

    df.loc[:, client_cols] = df.loc[:, client_cols].sub(df["client_clock_error"], axis=0)



def wvb_frame_times_to_delays(measurement, m=None, c=None):
    """Converts a measurement into a table of delays relative to the present_called column."""

    df = wvb_combine_frame_times(measurement)

    if m is not None and c is not None:
        # Fix client clock error
        wvb_fix_client_clock_error(df, m, c)

    # The "present_called" is the base reference of a frame.
    # All other frames (except "frame_id", "dropped", "frame_index" and "frame_delay")
    # should be modified to be relative offset to that.
    # This is done by subtracting the "present_called" value from the other columns.

    df = wvb_convert_time_to_delay(df)

    return df


def wvb_combine_delays(measurements):
    """Create a table where delays are averaged frame by frame"""

    df = None
    for m in measurements:
        df2 = wvb_frame_times_to_delays(m)
        if df is None:
            df = df2
        else:
            df = pd.concat([df, df2])

    # Average the values
    df = df.groupby(df.frame_id).mean()

    return df


def wvb_plot_frame_times(df, codec_name, n=10, start=0, plot_tracking=True):
    """Bar plot displaying the parallel execution of the video pipeline, as well
    as the latency taken by each pipeline stage."""

    # Plot cascading lines starting from x=present_called and begin end_frame wide
    fig, ax = plt.subplots(figsize=(10, 5))

    # Set custom color palette
    colors = [
        "#d5dbe0", # idle
        "#304493", # tracking collection
        "#e67e22", # tracking transmission
        "#b167ce", # rendering
        "#e74c3c", # encoding
        "#2db0d8", # video transmission
        "#2ecc71", # decoding
        "#1d7223", # display
    ]
    ax.set_prop_cycle(color=colors)

    steps = [
        # Label, start, width
        ("Idling",                df["present_called"] + df["tracking_sampled"],           df["end_frame"] - df["tracking_sampled"]),
        ("Tracking collection",   df["present_called"] + df["tracking_sampled"],           df["tracking_sampled"]           - df["tracking_sampled"]),
        ("Tracking transmission", df["present_called"] + df["tracking_sampled"] + 500,     df["driver_tracking_processed"]  - df["tracking_sampled"]),
        ("Rendering",             df["present_called"] + df["driver_pose_accessed"],       df["present_info_received"]    - df["driver_pose_accessed"]),
        ("Encoding",              df["present_called"] + df["shared_texture_acquired"],    df["after_last_get_next_packet"] - df["shared_texture_acquired"]),
        ("Video transmission",    df["present_called"] + df["after_last_get_next_packet"], df["last_packet_received"]       - df["after_last_get_next_packet"]),
        ("Decoding",              df["present_called"] + df["pushed_to_decoder"],          df["after_render"]               - df["pushed_to_decoder"]),
        ("Display",               df["present_called"] + df["after_render"],               df["end_frame"]     - df["after_render"]),
    ]

    # For rendering start, for nan rows, use df["present_called"]
    steps[3] = (steps[3][0], steps[3][1].fillna(df["present_called"]), steps[3][2].fillna(df["present_info_received"]))

    min_x = np.min(steps[1][1][start:start+n].dropna())

    y = np.arange(n)

    for label, x, width in steps:
        x = x[start:start+n]
        width = width[start:start+n]

        w = np.maximum(width, 700)

        ax.barh(y, w / 1000, left=(x - min_x) / 1000, height=0.8, label=label)

    # set axis labels and limits
    ax.set_xlabel('Time (ms)', fontsize=10)
    ax.set_ylabel("Frame number", fontsize=10)
    # set label for each y
    ax.set_yticks(y)
    ax.set_title(f'Video pipeline breakdown ({codec_name})', fontweight="500", fontsize=11)
    # set font size of ticks
    ax.set_yticks(y)
    ax.set_yticklabels(ax.get_yticks().astype(int), fontsize=10)
    # x tick don't show comma
    ax.set_xticklabels(ax.get_xticks().astype(int), fontsize=10)
    ax.spines['top'].set_visible(False)
    # ax1.spines['left'].set_visible(False)
    ax.spines['right'].set_visible(False)

    # Add color legend
    handles, labels = ax.get_legend_handles_labels()

    # Place first handle/label last
    handles = handles[1:] + handles[:1]
    labels = labels[1:] + labels[:1]

    ax.legend(handles[::], labels[::], loc='upper center', ncol=4, fontsize=10, bbox_to_anchor=(0.5, -0.12))


def wvb_avg_rtt(measurements):
    """Compute the average round trip time of the measurements."""

    rtts = []
    for m in measurements:
        table = m['network_measurements']
        if len(table) > 0:
            rtts.append(table['rtt'].astype(int).dropna().mean())

    return np.mean(rtts)


def wvb_med_clock_error(measurements):
    """Compute the median clock error of the measurements."""

    errors = []
    for m in measurements:
        table = m['network_measurements']
        if len(table) > 0:
            errors.append(table['clock_error'].astype(int).dropna().median())

    return np.mean(errors)


def wvb_plot_latency(measurement, codec_name, m=None, c=None):
    """Plot the latency of each frame."""

    df = wvb_frame_times_to_delays(measurement, m, c)

    # Create two plots
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(13, 7))
    fig.tight_layout(h_pad=3)

    df["frame_id"] = df["frame_id"] - df["frame_id"].iloc[0]

    # First, plot end_frame (delay after Present) vs frame index
    ax1.plot(df["frame_id"], df["end_frame"] /
             1000, label="Frame time")
    ax1.set_xlabel("Frame")
    ax1.set_ylabel("Delay (milliseconds)")
    ax1.set_title(f"Delay after Present ({codec_name})")

    # Motion to photon latency
    # Time between tracking_sampled and end_frame
    # Both of which are delays from present_called
    ax2.plot(df["frame_id"], (df["end_frame"] - df["tracking_sampled"]) / 1000,
             label="Motion to photon latency")
    ax2.set_xlabel("Frame")
    ax2.set_ylabel("Delay (milliseconds)")
    ax2.set_title(f"Motion to photon latency ({codec_name})")

    ax3.plot(df["frame_id"], (-df["tracking_sampled"]) /
             1000, label="Tracking latency")
    ax3.set_xlabel("Frame")
    ax3.set_ylabel("Delay (milliseconds)")
    ax3.set_title(f"Tracking latency ({codec_name})")


def wvb_plot_fps(measurement, codec_name):
    """Plot the FPS of the measurements."""

    df = wvb_frame_times_to_delays(measurement)

    df["frame_id"] = df["frame_id"] - df["frame_id"].iloc[0]

    # There are various definitions of FPS that we could use
    # - Rate of "end_frame" events
    # - Rate of "present_called" events

    # Create a triple graph for each of these
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 7))
    fig.tight_layout(h_pad=3)

    # Get delay between each "end_frame" and the previous one
    inter_frame_delay = (df["present_called"] + df["end_frame"]).diff()
    ax1.plot(df["frame_id"], 1000000 / inter_frame_delay, label="FPS")
    ax1.set_xlabel("Frame")
    ax1.set_ylabel("FPS")
    ax1.set_title(f"Client frames per second ({codec_name})")

    # Get inter frame delay, but filter out duplicates (frames with same present_called)
    inter_frame_delay = (df["present_called"]).diff()

    # Remove frame indices where present_called is the same as the previous one
    ax2.plot(df["frame_id"], 1000000 / inter_frame_delay, label="FPS")
    ax2.set_xlabel("Frame")
    ax2.set_ylabel("FPS")
    ax2.set_title(f"Driver frames per second ({codec_name})")


def wvb_plot_inter_frame_times(measurement, codec_name):
    """Plot the inter frame times of the measurements."""

    # Same as fps, but plot ms
    df = wvb_frame_times_to_delays(measurement)

    df["frame_id"] = df["frame_id"] - df["frame_id"].iloc[0]

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 7))
    fig.tight_layout(h_pad=3)

    inter_frame_delay = (df["present_called"] + df["end_frame"]).diff()
    ax1.plot(df["frame_id"], inter_frame_delay /
             1000, label="Inter frame delay")
    ax1.set_xlabel("Frame")
    ax1.set_ylabel("Delay (milliseconds)")
    ax1.set_title(f"Client inter frame delay ({codec_name})")

    inter_frame_delay = (df["present_called"]).diff()
    ax2.plot(df["frame_id"], inter_frame_delay /
             1000, label="Inter frame delay")
    ax2.set_xlabel("Frame")
    ax2.set_ylabel("Delay (milliseconds)")
    ax2.set_title(f"Driver inter frame delay ({codec_name})")


def wvb_load_rgba_image(path, width, height):
    """Loads a raw RGBA image from a file."""

    with open(path, "rb") as f:

        # Get size
        f.seek(0, 2)
        size = f.tell()

        if size != width * height * 4:
            raise ValueError("Invalid image size")

        # Read image
        f.seek(0)
        img = np.frombuffer(f.read(), dtype=np.uint8)
        img = img.reshape((height, width, 4))

    img = np.copy(img)

    # Set alpha to 255
    img[:, :, 3] = 255

    return img


def wvb_psnr(image1, image2):
    """Compute the PSNR between two images."""

    mse = np.mean((image1 - image2) ** 2)
    if mse == 0:
        return np.inf
    return 20 * np.log10(255.0 / np.sqrt(mse))


def wvb_ssim(image1, image2):
    """Compute the SSIM between two images."""

    return skimage.metrics.structural_similarity(image1, image2, multichannel=True, channel_axis=2)
