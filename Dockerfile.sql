FROM python:3.13-slim
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY pipeline.py .
ENV PYTHONDONTWRITEBYTECODE=1
CMD ["python", "-u", "/app/pipeline.py"]
