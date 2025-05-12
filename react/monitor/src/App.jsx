import React, { useEffect, useState } from "react";
import { Line } from "react-chartjs-2";
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Tooltip,
  Legend,
} from "chart.js";
import "./index.css";

ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Tooltip,
  Legend
);

const timeScales = [1, 2, 6, 12, 24]; // en horas

function App() {
  const [dataHistory, setDataHistory] = useState([]);
  const [fanValue, setFanValue] = useState(1);
  const [timeScale, setTimeScale] = useState(24); // escala en horas, por defecto 24h

  const API_BASE = "http://10.0.1.39";

  const fetchData = async () => {
    try {
      const res = await fetch(`${API_BASE}/data`);
      const json = await res.json();
      setDataHistory(json.history || []);
      if (json.fan !== undefined) {
        setFanValue(json.fan);
      }
    } catch (error) {
      console.error("Error al obtener datos:", error);
    }
  };

  const updateFan = async (value) => {
    try {
      await fetch(`${API_BASE}/setFan?value=${value}`);
    } catch (error) {
      console.error("Error actualizando fan:", error);
    }
  };

  useEffect(() => {
    fetchData();
    const interval = setInterval(() => {
      fetchData();
    }, 60000); // actualiza cada minuto
    return () => clearInterval(interval);
  }, []);

  // Filtrar la data en función de la escala de tiempo seleccionada.
  // Dado que cada registro es 1 minuto, se necesitan los últimos (timeScale * 60) registros.
  const filteredData = dataHistory.slice(Math.max(dataHistory.length - timeScale * 60, 0));

  // Generar etiquetas con base en la cantidad de registros filtrados.
  const now = Date.now();
  const labels = filteredData.map((_, i) => {
    const minutesAgo = filteredData.length - i - 1;
    const time = new Date(now - minutesAgo * 60000);
    return time.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  });

  const chartData = {
    labels,
    datasets: [
      {
        label: "Temperatura (°C)",
        data: filteredData.map(item => item.temp),
        borderColor: "rgba(255,99,132,1)",
        backgroundColor: "rgba(255,99,132,0.2)",
        yAxisID: "y", // escala izquierda para temperatura
      },
      {
        label: "Humedad (%)",
        data: filteredData.map(item => item.hum),
        borderColor: "rgba(54,162,235,1)",
        backgroundColor: "rgba(54,162,235,0.2)",
        yAxisID: "y1", // escala derecha para humedad
      },
    ],
  };

  const chartOptions = {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: { position: "top" },
      tooltip: { mode: "index", intersect: false },
    },
    scales: {
      x: {
        title: { display: true, text: "Tiempo" },
      },
      y: {
        type: "linear",
        position: "left",
        title: { display: true, text: "Temperatura (°C)" },
      },
      y1: {
        type: "linear",
        position: "right",
        title: { display: true, text: "Humedad (%)" },
        grid: { drawOnChartArea: false },
      },
    },
  };

  return (
    <div className="container" style={{ width: "100%", height: "100vh" }}>
      <h1>Monitor ESP32</h1>
      {/* Botones para escoger la escala de tiempo */}
      <div style={{ marginBottom: "20px", textAlign: "center" }}>
        {timeScales.map(scale => (
          <button
            key={scale}
            style={{
              margin: "0 5px",
              padding: "5px 10px",
              background: timeScale === scale ? "#007bff" : "#ccc",
              color: timeScale === scale ? "#fff" : "#000",
              border: "none",
              borderRadius: "4px",
            }}
            onClick={() => setTimeScale(scale)}
          >
            {scale}h
          </button>
        ))}
      </div>
      {/* Contenedor para el gráfico */}
      <div style={{ width: "100%", height: "50%" }}>
        <Line data={chartData} options={chartOptions} />
      </div>
      <div style={{ marginTop: "30px", textAlign: "center" }}>
        <h2>Control del Fancooler</h2>
        <input
          type="range"
          min="1"
          max="100"
          value={fanValue}
          onChange={(e) => setFanValue(e.target.value)}
          onMouseUp={() => updateFan(fanValue)}
          onTouchEnd={() => updateFan(fanValue)}
          style={{ width: "80%" }}
        />
        <p>Velocidad: {fanValue}%</p>
      </div>
    </div>
  );
}

export default App;