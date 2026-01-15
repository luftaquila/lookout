import os
import sys
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots


def parse_data_from_file(file_path):
    data_list = []

    if not os.path.exists(file_path):
        return pd.DataFrame()

    try:
        with open(file_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    timestamp_str, value_str = line.split(" : ")
                    data_list.append({"timestamp": timestamp_str.strip(), "value": float(value_str.strip())})
                except ValueError:
                    continue

        df = pd.DataFrame(data_list)
        if not df.empty:
            df["timestamp"] = pd.to_datetime(df["timestamp"])
        return df

    except Exception:
        return pd.DataFrame()


def generate_dashboard(input_file_path, output_html_path="report.html"):
    df = parse_data_from_file(input_file_path)

    if df.empty:
        return

    time_intervals = []
    for h in range(5, 21):
        time_intervals.append(f"{h:02d}:00")
        time_intervals.append(f"{h:02d}:30")

    df["time_str"] = df["timestamp"].dt.floor("30min").dt.strftime("%H:%M")

    df = df[df["time_str"].isin(time_intervals)]

    days_map = {0: "월", 1: "화", 2: "수", 3: "목", 4: "금", 5: "토", 6: "일"}
    df["weekday_num"] = df["timestamp"].dt.dayofweek
    df["weekday"] = df["weekday_num"].map(days_map)

    daily_avg = df.groupby("weekday_num")["value"].mean().reset_index()
    daily_avg["weekday"] = daily_avg["weekday_num"].map(days_map)

    hourly_avg = df.groupby("time_str")["value"].mean().reindex(time_intervals, fill_value=0).reset_index()

    heatmap_data = df.pivot_table(index="weekday_num", columns="time_str", values="value", aggfunc="mean")
    heatmap_data = heatmap_data.reindex(index=range(5), columns=time_intervals)

    fig = make_subplots(
        rows=2,
        cols=2,
        specs=[[{"colspan": 2}, None], [{}, {}]],
        subplot_titles=("<b>히트맵</b>", "<b>요일 평균</b>", "<b>시간대 평균</b>"),
        vertical_spacing=0.25,
        horizontal_spacing=0.15,
    )

    fig.add_trace(
        go.Heatmap(
            z=heatmap_data.values,
            x=time_intervals,
            y=[days_map[i] for i in range(5)],
            colorscale="Viridis",
            colorbar=dict(title="값", len=0.5, y=0.8),
            hoverongaps=False,
        ),
        row=1,
        col=1,
    )

    fig.add_trace(
        go.Bar(
            x=daily_avg["weekday"],
            y=daily_avg["value"],
            marker_color="indianred",
            text=daily_avg["value"].round(1),
            textposition="auto",
            name="요일",
        ),
        row=2,
        col=1,
    )

    fig.add_trace(
        go.Scatter(
            x=hourly_avg["time_str"],
            y=hourly_avg["value"],
            fill="tozeroy",
            mode="lines+markers",
            line=dict(color="royalblue"),
            name="시간대",
        ),
        row=2,
        col=2,
    )

    start_date = df["timestamp"].min().strftime("%Y-%m-%d")
    end_date = df["timestamp"].max().strftime("%Y-%m-%d")

    fig.update_layout(
        title_text=f"주차 현황 통계 ({start_date} ~ {end_date})",
        title_font_size=24,
        height=900,
        showlegend=False,
        template="plotly_white",
        margin=dict(t=100, b=50),
        autosize=True,
    )

    fig.update_xaxes(title_text="시간", dtick=2, row=1, col=1)
    fig.update_yaxes(title_text="요일", row=1, col=1, autorange="reversed")

    fig.update_xaxes(title_text="요일", row=2, col=1)
    fig.update_yaxes(title_text="평균", row=2, col=1)

    fig.update_xaxes(title_text="시간", dtick=4, row=2, col=2)
    fig.update_yaxes(title_text="평균", row=2, col=2)

    plot_html = fig.to_html(full_html=False, include_plotlyjs="cdn", config={"responsive": True})

    html_content = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>주차 현황 통계</title>
        <style>
            body {{
                font-family: 'Open Sans', verdana, arial, sans-serif;
                background-color: #f4f6f9;
                margin: 0;
                padding: 20px;
            }}
            .container {{
                max-width: 1200px;
                width: 100%;
                margin: 0 auto;
                background-color: white;
                box-shadow: 0 4px 6px rgba(0,0,0,0.1);
                border-radius: 8px;
                padding: 20px;
                box-sizing: border-box;
            }}
            .plotly-graph-div {{
                width: 100%;
            }}
        </style>
    </head>
    <body>
        <div class="container">
            {plot_html}
        </div>
    </body>
    </html>
    """

    with open(output_html_path, "w", encoding="utf-8") as f:
        f.write(html_content)

    print(f"File created: {output_html_path}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(1)

    input_filename = sys.argv[1]

    if not os.path.exists(input_filename):
        sys.exit(1)

    generate_dashboard(input_filename, "data/report.html")
