import { Navigate, useNavigate, useParams } from "react-router";
import ApiDoc from "../components/ApiDoc.jsx";
import DatabaseDoc from "../components/DatabaseDoc.jsx";
import StreamDoc from "../components/StreamDoc.jsx";
import "../docs.css";

const SECTIONS = [
  { id: "database", label: "Database schema", hint: "Live tables, keys and row counts", Doc: DatabaseDoc },
  { id: "api", label: "REST API (Swagger)", hint: "Every endpoint, try it out", Doc: ApiDoc, wide: true },
  { id: "stream", label: "Live stream", hint: "The WebSocket messages", Doc: StreamDoc },
];

/** Reference documentation read from the running system. The section is in the URL: /docs/api. */
export default function DocsPage() {
  const { section } = useParams();
  const navigate = useNavigate();
  const current = SECTIONS.find((s) => s.id === section);
  if (!current) return <Navigate to={`/docs/${SECTIONS[0].id}`} replace />;
  const { Doc } = current;

  return (
    <div className="docs feature">
      <nav className="docs-nav" aria-label="Documentation sections">
        <div className="docs-nav-title">Documentation</div>
        {SECTIONS.map((s) => (
          <button
            key={s.id}
            className={s.id === current.id ? "on" : ""}
            aria-current={s.id === current.id ? "page" : undefined}
            onClick={() => navigate(`/docs/${s.id}`)}
          >
            <span className="dn-label">{s.label}</span>
            <span className="dn-hint">{s.hint}</span>
          </button>
        ))}
      </nav>
      <div className={`docs-body ${current.wide ? "wide" : ""}`}>
        <Doc />
      </div>
    </div>
  );
}
